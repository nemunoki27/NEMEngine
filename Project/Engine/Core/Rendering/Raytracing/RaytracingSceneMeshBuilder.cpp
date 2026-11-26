#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingSceneGeometryUtility.h"
#include <Engine/Core/World/Components/Rendering/HexTilingComponent.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>
#include <cstddef>
#include <functional>
#include <span>
#include <variant>

//============================================================================
//	RaytracingSceneBuilder internal
//============================================================================
using namespace Engine::RaytracingSceneGeometryUtility;

namespace {
	constexpr uint32_t kMaxConsecutiveBLASRefits = 240;
}

void Engine::RaytracingSceneBuilder::BuildMeshInstances(
	std::span<const CollectedMeshInstance> sceneMeshes, SceneBuildWork& work) {

	for (const CollectedMeshInstance& src : sceneMeshes) {

		// メッシュリソースを取得
		const MeshGPUResource* meshResource = work.meshBackend->FindMeshResource(src.meshAssetID);
		if (!meshResource) {
			continue;
		}
		if (!meshResource->vertexSRV.buffer || !meshResource->indexSRV.buffer) {
			continue;
		}
		if (meshResource->subMeshes.empty()) {
			continue;
		}
		// TLASが参照する頂点とIndexの所有を保持する
		work.scene.buildingMeshOwnerships.push_back(meshResource->ownership);
		const std::span<const SubMeshMaterial> subMeshes =
			src.world ? GetMeshSubMeshes(*src.world, src.entity) : std::span<const SubMeshMaterial>{};
		uint32_t visibleSubMeshCount = 0;
		for (uint32_t index = 0; index < static_cast<uint32_t>(meshResource->subMeshes.size()); ++index) {

			if (subMeshes.size() <= index || subMeshes[index].visible) {
				++visibleSubMeshCount;
			}
		}
		if (visibleSubMeshCount == 0) {
			continue;
		}
		work.staticScene = work.staticScene && !meshResource->isSkinned;
		work.blasGeometryCount += visibleSubMeshCount;
		const uint64_t geometryTransformHash =
			ComputeGeometryLayoutHash(subMeshes, static_cast<uint32_t>(meshResource->subMeshes.size()));
		uint64_t geometryLayoutHash = geometryTransformHash;
		const bool hasCustomGeometryTransforms =
			geometryTransformHash != ComputeGeometryLayoutHash({}, static_cast<uint32_t>(meshResource->subMeshes.size()));
		StaticInstanceBLASKey staticInstanceKey{};
		StaticInstanceBLASEntry* staticInstanceEntry = nullptr;
		bool usesInstanceBLAS = false;

		SkinnedVertexSource skinnedSource{};

		// Skinning済みの頂点を取得
		bool hasSkinnedSource = meshResource->isSkinned && work.meshBackend->FindSkinnedVertexSource(
															   src.world, src.entity, src.meshAssetID, skinnedSource);

		// ホットリロードで世代が変わったら、このメッシュの旧世代BLASを破棄してから作り直す
		const uint32_t reloadGeneration = meshResource->reloadGeneration;
		auto generationIt = blasCache_.meshBLASGeneration_.find(src.meshAssetID);
		if (generationIt != blasCache_.meshBLASGeneration_.end() && generationIt->second != reloadGeneration) {

			std::erase_if(blasCache_.blases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
			});
			std::erase_if(blasCache_.dynamicBlases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
			});
			std::erase_if(blasCache_.staticInstanceBLASes_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
			});
		}
		blasCache_.meshBLASGeneration_[src.meshAssetID] = reloadGeneration;

		// 1メッシュの全サブメッシュを1つのBLASへまとめる
		std::vector<RaytracingBLASGeometryInput> geometries{};
		geometries.reserve(visibleSubMeshCount);

		const uint32_t geometryDataOffset = static_cast<uint32_t>(work.scene.result.sceneGeometryScratch_.size());
		const uint32_t pickRecordOffset = static_cast<uint32_t>(work.scene.result.scenePickRecords_.size());

		const uint32_t vertexDescriptorIndex = hasSkinnedSource ? skinnedSource.srvIndex : meshResource->vertexSRV.srvIndex;
		const uint32_t vertexOffset = hasSkinnedSource ? skinnedSource.vertexOffset : 0;
		const D3D12_GPU_VIRTUAL_ADDRESS vertexAddress =
			hasSkinnedSource
				? skinnedSource.gpuAddress + sizeof(MeshVertex) * static_cast<uint64_t>(skinnedSource.vertexOffset) +
					  offsetof(MeshVertex, position)
				: meshResource->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress() + offsetof(MeshVertex, position);
		const D3D12_GPU_VIRTUAL_ADDRESS indexAddress = meshResource->indexBuffer.GetResource()->GetGPUVirtualAddress();
		const uint32_t indexSize = meshResource->indexBuffer.GetIndexSizeInBytes();

		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(meshResource->subMeshes.size()); ++subMeshIndex) {

			if (subMeshIndex < subMeshes.size() && !subMeshes[subMeshIndex].visible) {
				continue;
			}

			const SubMeshDesc& importedSubMesh = meshResource->subMeshes[subMeshIndex];
			const bool hasMesh = subMeshIndex < subMeshes.size();
			const Matrix4x4 localMatrix =
				hasMesh ? MeshSubMeshRuntime::BuildRenderLocalMatrix(subMeshes[subMeshIndex]) : Matrix4x4::Identity();

			RaytracingBLASGeometryInput geometry{};
			geometry.vertexAddress = vertexAddress;
			geometry.vertexStride = sizeof(MeshVertex);
			geometry.vertexCount = meshResource->vertexCount;
			geometry.indexAddress = indexAddress + static_cast<uint64_t>(indexSize) * importedSubMesh.indexOffset;
			geometry.indexCount = importedSubMesh.indexCount;
			geometry.indexFormat = meshResource->indexBuffer.GetFormat();
			geometry.localMatrix = localMatrix;
			geometries.emplace_back(geometry);

			const uint32_t subMeshDataIndex = static_cast<uint32_t>(work.scene.result.sceneSubMeshScratch_.size());

			MeshSubMeshRenderState renderState{};
			if (src.renderer) {
				renderState.material = src.renderer->material;
				renderState.surfaceMode = src.renderer->queue == RenderPhase::Transparent ?
					MaterialSurfaceMode::Transparent : MaterialSurfaceMode::Opaque;
				if (hasMesh) renderState = MeshDrawPathCommon::ResolveSubMeshRenderState(*src.renderer, subMeshes[subMeshIndex]);
			}
			const auto* material = work.assetLibrary.LoadMaterial(work.materialResolver.ResolveORDefault(
				work.assetDatabase, renderState.material, DefaultMaterialSlot::Mesh));
			if (material && material->renderState.overridesRenderer && !renderState.surfaceModeOverridden) {
				renderState.surfaceMode = material->renderState.surfaceMode;
			}
			// 描画と同じMaterial既定値とサブメッシュ上書きを使用する
			const auto* overrides = hasMesh ? &subMeshes[subMeshIndex].materialInstance : nullptr;
			const auto uvMatrix = hasMesh ? MeshSubMeshRuntime::BuildUVMatrix(subMeshes[subMeshIndex]) : Matrix4x4::Identity();
			MeshSubMeshShaderData subMeshData{};
			if (material) {
				CollectRenderTextureParameters(material->parameters, work.assetDatabase, work.scene.renderTextureInputs);
				subMeshData = work.scene.materialResolver.BuildSubMeshData(work.graphicsCore, work.assetDatabase,
					*material, overrides, uvMatrix);
			}
			if (overrides) CollectRenderTextureParameters(*overrides, work.assetDatabase, work.scene.renderTextureInputs);
			if (src.world) subMeshData.hexTiling = ResolveHexTiling(*src.world, src.entity, static_cast<int32_t>(subMeshIndex));
			subMeshData.localMatrix = localMatrix;
			const MeshNormalMatrixResult localNormal = BuildSafeMeshNormalMatrix(localMatrix);
			subMeshData.localNormalMatrix = localNormal.matrix;
			subMeshData.localOrientationSign = localNormal.orientationSign;
			if (hasMesh) subMeshData.sourcePivot = subMeshes[subMeshIndex].sourcePivot;
			subMeshData.renderGroupIndex = static_cast<uint32_t>(material && material->renderState.overridesRenderer ?
				material->renderState.blendMode : (src.renderer ? src.renderer->blendMode : BlendMode::Normal));
			subMeshData.useRasterHitMaterial = material && !FindPass(*material, MaterialPassKind::RayTracing) &&
				!MaterialRenderPolicy::IsStandardSurface(*material) && renderState.surfaceMode != MaterialSurfaceMode::Transparent;
			const float alphaCutoff = hasMesh ? subMeshes[subMeshIndex].alphaCutoff : 0.5f;
			subMeshData._materialPad = (static_cast<uint32_t>(renderState.surfaceMode) << 16u) |
				static_cast<uint32_t>(std::clamp(alphaCutoff, 0.0f, 1.0f) * 65535.0f);
			// 確実に不透明な標準MaterialだけをHWで確定する
			geometries.back().flags = MaterialRenderPolicy::IsOpaqueGeometry(material, renderState.surfaceMode) ?
				D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
			Algorithm::HashCombine(geometryLayoutHash, static_cast<uint32_t>(geometries.back().flags));
			// サブメッシュデータを追加
			work.scene.result.sceneSubMeshScratch_.emplace_back(subMeshData);

			// メッシュピック用のサブメッシュ情報を追加
			MeshSubMeshPickRecord pickRecord{};
			pickRecord.entity = src.entity;
			pickRecord.subMeshIndex = subMeshIndex;
			if (hasMesh) {

				pickRecord.subMeshStableID = subMeshes[subMeshIndex].stableID;
			}
			work.scene.result.scenePickRecords_.emplace_back(pickRecord);

			RaytracingGeometryShaderData geometryData{};
			geometryData.subMeshDataIndex = subMeshDataIndex;
			AppendHitMaterial(work, material, overrides, geometryData);
			geometryData.indexOffset = importedSubMesh.indexOffset;
			geometryData.pickRecordIndex = static_cast<uint32_t>(work.scene.result.scenePickRecords_.size() - 1);
			RaytracingGeometryBounds bounds;
			bounds.center = importedSubMesh.boundsCenter;
			bounds.radius = importedSubMesh.boundsRadius;
			MeshBoundsUtility::TransformSphere(localMatrix, bounds.center, bounds.radius);
			geometryData.boundsSphere = {bounds.center.x, bounds.center.y, bounds.center.z, bounds.radius};
			work.scene.result.sceneGeometryScratch_.emplace_back(geometryData);
			bounds.emissiveTriangleCount = GetEmissiveTriangleCount(subMeshData, importedSubMesh.indexCount);
			bounds.deformed = hasSkinnedSource;
			bounds.instanceIndex = static_cast<uint32_t>(work.scene.result.sceneInstanceScratch_.size());
			bounds.receiveDecals = src.renderer && src.renderer->receiveDecals && !meshResource->isSkinned &&
				renderState.surfaceMode != MaterialSurfaceMode::Transparent;
			work.scene.result.sceneGeometryBounds_.push_back(bounds);
		}

		if (!meshResource->isSkinned && hasCustomGeometryTransforms) {

			staticInstanceKey.world = src.world;
			staticInstanceKey.worldLifetime = src.world ? src.world->GetLifetime() : nullptr;
			staticInstanceKey.entity = src.entity;
			staticInstanceKey.meshAssetID = src.meshAssetID;
			staticInstanceKey.reloadGeneration = meshResource->reloadGeneration;
			StaticInstanceBLASEntry& entry = blasCache_.staticInstanceBLASes_[staticInstanceKey];
			entry.lastUsedFrame = GraphicsFrameState::GetFrameSerial();
			if (!entry.layoutInitialized) {
				entry.geometryLayoutHash = geometryTransformHash;
				entry.layoutInitialized = true;
			} else if (entry.geometryLayoutHash != geometryTransformHash) {
				entry.dedicated = true;
			}
			staticInstanceEntry = &entry;
			usesInstanceBLAS = entry.dedicated;
		}
		Algorithm::HashCombine(work.geometryContentHash, std::hash<AssetID>{}(src.meshAssetID));
		Algorithm::HashCombine(work.geometryContentHash, reloadGeneration);
		Algorithm::HashCombine(work.geometryContentHash, geometryLayoutHash);
		Algorithm::HashCombine(work.geometryContentHash, vertexAddress);
		if (hasSkinnedSource) {
			Algorithm::HashCombine(work.geometryContentHash, skinnedSource.poseGeneration);
			Algorithm::HashCombine(work.geometryContentHash, skinnedSource.bufferGeneration);
		}
		RaytracingBLASInput input{};
		input.geometries = geometries;
		input.allowUpdate = hasSkinnedSource;
		input.submission = work.graphicsCore.GetDXObject().GetDxCommand()->GetSubmissionTicket();

		ID3D12Resource* blasResource = nullptr;
		if (hasSkinnedSource) {

			DynamicBLASKey key{};
			key.world = src.world;
			key.worldLifetime = src.world ? src.world->GetLifetime() : nullptr;
			key.entity = src.entity;
			key.meshAssetID = src.meshAssetID;
			key.reloadGeneration = reloadGeneration;

			DynamicBLASEntry& entry = blasCache_.dynamicBlases_[key];
			entry.lastUsedFrame = GraphicsFrameState::GetFrameSerial();
			if (!entry.blas.IsBuilt()) {

				entry.blas.SetRetirementQueue(work.graphicsCore.GetDXObject().GetResourceRetirement());
				entry.blas.Build(work.device, work.commandList, input);
				FrameProfiler::GetInstance().AddBLASBuild(static_cast<uint32_t>(geometries.size()));
				work.requireTLASRebuild = true;
				entry.consecutiveRefitCount = 0;
			} else if (entry.poseGeneration != skinnedSource.poseGeneration ||
					   entry.bufferGeneration != skinnedSource.bufferGeneration ||
					   entry.geometryLayoutHash != geometryLayoutHash || entry.vertexAddress != vertexAddress) {

				if (kMaxConsecutiveBLASRefits <= entry.consecutiveRefitCount + 1) {

					entry.blas.Rebuild(work.commandList, input);
					entry.consecutiveRefitCount = 0;
					FrameProfiler::GetInstance().AddBLASBuild(static_cast<uint32_t>(geometries.size()));
				} else {

					entry.blas.Update(work.commandList, input);
					++entry.consecutiveRefitCount;
					FrameProfiler::GetInstance().AddBLASRefit(static_cast<uint32_t>(geometries.size()));
				}
				work.blasContentsChanged = true;
			} else {

				FrameProfiler::GetInstance().AddBLASSkip(static_cast<uint32_t>(geometries.size()));
			}
			entry.poseGeneration = skinnedSource.poseGeneration;
			entry.bufferGeneration = skinnedSource.bufferGeneration;
			entry.geometryLayoutHash = geometryLayoutHash;
			entry.vertexAddress = vertexAddress;
			blasResource = entry.blas.GetResource();
		} else if (usesInstanceBLAS) {

			StaticInstanceBLASEntry& entry = *staticInstanceEntry;
			RaytracingBLASInput instanceInput = input;
			instanceInput.allowUpdate = true;

			BottomLevelAccelerationStructure& blas = entry.blas;
			const bool geometryChanged = entry.publishedGeometryLayoutHash != geometryLayoutHash;
			if (!blas.IsBuilt()) {

				blas.SetRetirementQueue(work.graphicsCore.GetDXObject().GetResourceRetirement());
				blas.Build(work.device, work.commandList, instanceInput);
				FrameProfiler::GetInstance().AddBLASBuild(static_cast<uint32_t>(geometries.size()));
				work.requireTLASRebuild = true;
			} else if (geometryChanged) {

				blas.Update(work.commandList, instanceInput);
				work.blasContentsChanged = true;
				FrameProfiler::GetInstance().AddBLASRefit(static_cast<uint32_t>(geometries.size()));
			} else {

				FrameProfiler::GetInstance().AddBLASSkip(static_cast<uint32_t>(geometries.size()));
			}
			entry.publishedGeometryLayoutHash = geometryLayoutHash;
			blasResource = blas.GetResource();
			entry.geometryLayoutHash = geometryTransformHash;
		} else {

			BLASKey key{};
			key.meshAssetID = src.meshAssetID;
			key.reloadGeneration = reloadGeneration;
			key.geometryLayoutHash = geometryLayoutHash;
			blasCache_.sharedBLASFrames_[key] = GraphicsFrameState::GetFrameSerial();
			work.scene.publishedBLASKeys.push_back(key);

			BottomLevelAccelerationStructure& blas = blasCache_.blases_[key];
			if (!blas.IsBuilt()) {

				blas.SetRetirementQueue(work.graphicsCore.GetDXObject().GetResourceRetirement());
				blas.Build(work.device, work.commandList, input);
				FrameProfiler::GetInstance().AddBLASBuild(static_cast<uint32_t>(geometries.size()));
				work.requireTLASRebuild = true;
			} else {

				FrameProfiler::GetInstance().AddBLASSkip(static_cast<uint32_t>(geometries.size()));
			}
			blasResource = blas.GetResource();
		}
		if (!blasResource) {
			continue;
		}

		RaytracingInstanceShaderData instanceShaderData{};
		instanceShaderData.vertexDescriptorIndex = vertexDescriptorIndex;
		instanceShaderData.indexDescriptorIndex = meshResource->indexSRV.srvIndex;
		instanceShaderData.vertexOffset = vertexOffset;
		instanceShaderData.geometryDataOffset = geometryDataOffset;
		instanceShaderData.entityIndex = src.entity.index;
		instanceShaderData.entityGeneration = src.entity.generation;
		const MeshRenderFlags renderFlags = src.renderer ? src.renderer->renderFlags : MeshRenderFlags::Default;
		const uint32_t renderingLayerMask = src.renderer ? src.renderer->renderingLayerMask : kRenderingLayerMaskBits;
		instanceShaderData.renderFlags = ToRaytracingRenderFlags(renderFlags, renderingLayerMask);
		const uint32_t shaderInstanceIndex = static_cast<uint32_t>(work.scene.result.sceneInstanceScratch_.size());
		work.scene.result.sceneInstanceScratch_.emplace_back(instanceShaderData);
		work.scene.result.scenePickRecordOffsets_.emplace_back(pickRecordOffset);

		RaytracingTLASInstance instance{};
		instance.blas = blasResource;
		instance.instanceID = shaderInstanceIndex;
		instance.hitGroupIndex = geometryDataOffset;
		instance.mask = kRaytracingMaskAlwaysHit;
		if (src.renderer) {
			if (src.castShadows) {
				instance.mask |= kRaytracingMaskShadowCaster;
			}
			if (HasMeshRenderFlag(src.renderer->renderFlags, MeshRenderFlags::CastReflection)) {
				instance.mask |= kRaytracingMaskReflectionCaster;
			}
		} else {

			instance.mask |= kRaytracingMaskShadowCaster | kRaytracingMaskReflectionCaster;
		}
		instance.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance.worldMatrix = src.worldMatrix;
		work.tlasInstances.emplace_back(instance);
		work.tlasEntityKeys.emplace_back(SceneEntityKey{ .world = src.world, .entity = src.entity, });
		if (!meshResource->isSkinned && hasCustomGeometryTransforms) {

			CachedMeshInstance instanceRecord{};
			instanceRecord.meshAssetID = src.meshAssetID;
			instanceRecord.world = src.world;
			instanceRecord.entity = src.entity;
			instanceRecord.reloadGeneration = reloadGeneration;
			instanceRecord.tracksInstanceLayout = true;
			work.meshInstances.emplace_back(instanceRecord);
		}
	}
}
