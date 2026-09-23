#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include "RaytracingSceneGeometryUtility.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <bit>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <unordered_set>
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
		work.staticScene = work.staticScene && !meshResource->isSkinned;
		work.blasGeometryCount += static_cast<uint32_t>(meshResource->subMeshes.size());
		const std::span<const SubMeshMaterial> subMeshes =
			src.world ? GetMeshSubMeshes(*src.world, src.entity) :
			std::span<const SubMeshMaterial>{};
		const uint64_t geometryLayoutHash = ComputeGeometryLayoutHash(
			subMeshes, static_cast<uint32_t>(meshResource->subMeshes.size()));
		const bool hasCustomGeometryTransforms =
			geometryLayoutHash != ComputeGeometryLayoutHash({},
				static_cast<uint32_t>(meshResource->subMeshes.size()));
		StaticInstanceBLASKey staticInstanceKey{};
		StaticInstanceBLASEntry* staticInstanceEntry = nullptr;
		bool usesInstanceBLAS = false;
		if (!meshResource->isSkinned && hasCustomGeometryTransforms) {

			staticInstanceKey.world = src.world;
			staticInstanceKey.entity = src.entity;
			staticInstanceKey.meshAssetID = src.meshAssetID;
			staticInstanceKey.reloadGeneration =
				meshResource->reloadGeneration;
			StaticInstanceBLASEntry& entry =
				blasCache_.staticInstanceBLASes_[staticInstanceKey];
			entry.lastUsedFrame = GraphicsFrameState::GetFrameSerial();
			if (!entry.layoutInitialized) {
				entry.geometryLayoutHash = geometryLayoutHash;
				entry.layoutInitialized = true;
			} else if (entry.geometryLayoutHash != geometryLayoutHash) {
				entry.dedicated = true;
			}
			staticInstanceEntry = &entry;
			usesInstanceBLAS = entry.dedicated;
		}
		Vector3 worldBoundsCenter{};
		float worldBoundsRadius = 0.0f;
		CalculateMeshWorldBounds(*meshResource,
			subMeshes, src.worldMatrix,
			worldBoundsCenter, worldBoundsRadius);
		const uint32_t selectedLOD =
			meshResource->isSkinned ? 0 :
			ResolveMeshLOD(work.runtimeFeatures, work.lodView,
				worldBoundsCenter, worldBoundsRadius);

		SkinnedVertexSource skinnedSource{};

		// スキンメッシュの頂点ソースを持っているか
		// リソース情報をアウトプットする
		bool hasSkinnedSource = meshResource->isSkinned && work.meshBackend->FindSkinnedVertexSource(
			src.world, src.entity, src.meshAssetID, skinnedSource);

		// ホットリロードで世代が変わったら、このメッシュの旧世代BLASを破棄してから作り直す
		const uint32_t reloadGeneration = meshResource->reloadGeneration;
		auto generationIt = blasCache_.meshBlasGeneration_.find(src.meshAssetID);
		if (generationIt != blasCache_.meshBlasGeneration_.end() && generationIt->second != reloadGeneration) {

			std::erase_if(blasCache_.blases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
				});
			std::erase_if(blasCache_.dynamicBlases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
				});
			std::erase_if(blasCache_.staticInstanceBLASes_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID &&
					pair.first.reloadGeneration != reloadGeneration;
				});
		}
		blasCache_.meshBlasGeneration_[src.meshAssetID] = reloadGeneration;

		// 1メッシュの全サブメッシュを1つのBLASへまとめる
		std::vector<RaytracingBLASGeometryInput> geometries{};
		geometries.reserve(meshResource->subMeshes.size());

		const uint32_t geometryDataOffset =
			static_cast<uint32_t>(result_.sceneGeometryScratch_.size());
		const uint32_t pickRecordOffset =
			static_cast<uint32_t>(result_.scenePickRecords_.size());

		const uint32_t vertexDescriptorIndex =
			hasSkinnedSource ? skinnedSource.srvIndex : meshResource->vertexSRV.srvIndex;
		const uint32_t vertexOffset =
			hasSkinnedSource ? skinnedSource.vertexOffset : 0;
		const D3D12_GPU_VIRTUAL_ADDRESS vertexAddress =
			hasSkinnedSource ?
			skinnedSource.gpuAddress +
				sizeof(MeshVertex) * static_cast<uint64_t>(skinnedSource.vertexOffset) +
				offsetof(MeshVertex, position) :
			meshResource->vertexSRV.buffer->GetResource()->GetGPUVirtualAddress() +
				offsetof(MeshVertex, position);
		const D3D12_GPU_VIRTUAL_ADDRESS indexAddress =
			meshResource->indexBuffer.GetResource()->GetGPUVirtualAddress();
		const uint32_t indexSize = meshResource->indexBuffer.GetIndexSizeInBytes();

		for (uint32_t subMeshIndex = 0;
			subMeshIndex < static_cast<uint32_t>(meshResource->subMeshes.size());
			++subMeshIndex) {

			const SubMeshDesc& importedSubMesh = meshResource->subMeshes[subMeshIndex];
			const bool hasMesh = subMeshIndex < subMeshes.size();
			const Matrix4x4 localMatrix = hasMesh ?
				MeshSubMeshRuntime::BuildRenderLocalMatrix(subMeshes[subMeshIndex]) :
				Matrix4x4::Identity();

			RaytracingBLASGeometryInput geometry{};
			geometry.vertexAddress = vertexAddress;
			geometry.vertexStride = sizeof(MeshVertex);
			geometry.vertexCount = meshResource->vertexCount;
			const MeshLODRange& selectedRange =
				ResolveRaytracingLODRange(
					importedSubMesh, selectedLOD);
			geometry.indexAddress = indexAddress +
				static_cast<uint64_t>(indexSize) *
					selectedRange.indexOffset;
			geometry.indexCount =
				selectedRange.indexCount;
			geometry.indexFormat = meshResource->indexBuffer.GetFormat();
			geometry.localMatrix = localMatrix;
			geometries.emplace_back(geometry);

			const uint32_t subMeshDataIndex =
				static_cast<uint32_t>(result_.sceneSubMeshScratch_.size());

			// サブメッシュデータを構築
			MeshSubMeshShaderData subMeshData{};
			subMeshData.importedBaseColor = importedSubMesh.baseColor;
			AssetID baseColorTextureAsset =
				MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(
					*meshResource, subMeshes, subMeshIndex);
			if (baseColorTextureAsset) {

				subMeshData.baseColorTextureIndex = materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, baseColorTextureAsset, true);
			} else if (MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(
				*meshResource, subMeshes, subMeshIndex)) {

				// 宣言はあるが見つからない:エラーテクスチャ
				subMeshData.baseColorTextureIndex = materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, AssetID{}, true);
			} else {

				// テクスチャ未設定:シェーダ側でimportedBaseColor*colorを使う
				subMeshData.baseColorTextureIndex = UINT32_MAX;
			}

			AssetID normalAsset = MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			AssetID metallicRoughnessAsset =
				MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(
					*meshResource, subMeshes, subMeshIndex);
			AssetID metallicAsset =
				MeshDrawPathCommon::ResolveSubMeshMetallicTextureAssetID(
					*meshResource, subMeshes, subMeshIndex);
			AssetID roughnessAsset =
				MeshDrawPathCommon::ResolveSubMeshRoughnessTextureAssetID(
					*meshResource, subMeshes, subMeshIndex);
			AssetID emissiveAsset = MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			AssetID occlusionAsset = MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			AssetID specularAsset = MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			subMeshData.normalTextureIndex = normalAsset ?
				materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, normalAsset, false) : UINT32_MAX;
			subMeshData.metallicRoughnessTextureIndex = metallicRoughnessAsset ?
				materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, metallicRoughnessAsset, false) : UINT32_MAX;
			subMeshData.metallicTextureIndex = metallicAsset ?
				materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, metallicAsset, false) : UINT32_MAX;
			subMeshData.roughnessTextureIndex = roughnessAsset ?
				materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, roughnessAsset, false) : UINT32_MAX;
			subMeshData.emissiveTextureIndex = emissiveAsset ?
				materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, emissiveAsset, true) : UINT32_MAX;
			subMeshData.occlusionTextureIndex = occlusionAsset ?
				materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, occlusionAsset, false) : UINT32_MAX;
			subMeshData.specularTextureIndex = specularAsset ?
				materialResolver_.ResolveTextureDescriptorIndex(
					work.graphicsCore, work.assetDatabase, specularAsset, false) : UINT32_MAX;

			// 初期値でCPU側のMeshSubMeshShaderDataとHLSLのSubMeshShaderDataは同一レイアウトに保つ
			subMeshData.localMatrix = Matrix4x4::Identity();
			subMeshData.localNormalMatrix = Matrix4x4::Identity();
			subMeshData.color = Color4::White();
			subMeshData.emissiveColor = Color4(0.0f, 0.0f, 0.0f, 0.0f);
			subMeshData.uvMatrix = Matrix4x4::Identity();
			if (hasMesh) {

				const auto& authoring = subMeshes[subMeshIndex];
				// RTはfixedなSubMeshShaderDataを使うので標準Parameter IDから値を詰める
				const auto& params = authoring.materialInstance;
				auto findColor = [&](MaterialParameterID id,
					const Color4& fallback) -> Color4 {

					const MaterialParameterValue* value =
						params.Find(id);
					return value &&
						std::holds_alternative<Color4>(
							value->value) ?
						std::get<Color4>(value->value) :
						fallback;
					};
				auto findFloat = [&](MaterialParameterID id,
					float fallback) -> float {

					const MaterialParameterValue* value =
						params.Find(id);
					return value &&
						std::holds_alternative<float>(
							value->value) ?
						std::get<float>(value->value) :
						fallback;
					};
				// テクスチャindexはMeshDrawPathCommonのresolverがmaterialInstanceを見て解決済み
				subMeshData.color = findColor(
					MaterialParameterIDs::BaseColor,
					Color4::White());
				subMeshData.emissiveColor = findColor(
					MaterialParameterIDs::EmissiveColor,
					Color4(0.0f, 0.0f, 0.0f, 0.0f));
				// alphaはRT用の発光強度として使いRGBの色と同じバッファへ詰める
				subMeshData.emissiveColor.a = findFloat(
					MaterialParameterIDs::EmissiveIntensity,
					1.0f);
				subMeshData.metallic = findFloat(
					MaterialParameterIDs::Metallic,
					subMeshData.metallic);
				subMeshData.roughness = findFloat(
					MaterialParameterIDs::Roughness,
					subMeshData.roughness);
				subMeshData.uvMatrix = MeshSubMeshRuntime::BuildUVMatrix(authoring);
				subMeshData.localMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(authoring);
				const MeshNormalMatrixResult localNormal = BuildSafeMeshNormalMatrix(subMeshData.localMatrix);
				subMeshData.localNormalMatrix = localNormal.matrix;
				subMeshData.localOrientationSign = localNormal.orientationSign;
				subMeshData.sourcePivot = authoring.sourcePivot;
			}
			// サブメッシュデータを追加
			result_.sceneSubMeshScratch_.emplace_back(subMeshData);

			// メッシュピック用のサブメッシュ情報を追加
			MeshSubMeshPickRecord pickRecord{};
			pickRecord.entity = src.entity;
			pickRecord.subMeshIndex = subMeshIndex;
			if (hasMesh) {

				pickRecord.subMeshStableID = subMeshes[subMeshIndex].stableID;
			}
			result_.scenePickRecords_.emplace_back(pickRecord);

			RaytracingGeometryShaderData geometryData{};
			geometryData.subMeshDataIndex = subMeshDataIndex;
			geometryData.indexOffset =
				selectedRange.indexOffset;
			geometryData.pickRecordIndex =
				static_cast<uint32_t>(result_.scenePickRecords_.size() - 1);
			result_.sceneGeometryScratch_.emplace_back(geometryData);
		}

		RaytracingBLASInput input{};
		input.geometries = geometries;
		input.allowUpdate = hasSkinnedSource;
		auto buildLODGeometries = [&](uint32_t lodIndex) {

			// 編集中は表示LODだけをrefitし、未使用LODのGPU更新を次回選択時まで遅延する
			std::vector<RaytracingBLASGeometryInput> lodGeometries =
				geometries;
			for (uint32_t subMeshIndex = 0;
				subMeshIndex < static_cast<uint32_t>(lodGeometries.size());
				++subMeshIndex) {

				const MeshLODRange& range = ResolveRaytracingLODRange(
					meshResource->subMeshes[subMeshIndex], lodIndex);
				lodGeometries[subMeshIndex].indexAddress =
					indexAddress + static_cast<uint64_t>(indexSize) *
						range.indexOffset;
				lodGeometries[subMeshIndex].indexCount = range.indexCount;
			}
			return lodGeometries;
		};

		ID3D12Resource* blasResource = nullptr;
		if (hasSkinnedSource) {

			DynamicBLASKey key{};
			key.world = src.world;
			key.entity = src.entity;
			key.meshAssetID = src.meshAssetID;
			key.reloadGeneration = reloadGeneration;

			DynamicBLASEntry& entry = blasCache_.dynamicBlases_[key];
			entry.lastUsedFrame =
				GraphicsFrameState::GetFrameSerial();
			if (!entry.blas.IsBuilt()) {

				entry.blas.SetRetirementQueue(work.graphicsCore.GetDXObject().GetResourceRetirement());
				entry.blas.Build(work.device, work.commandList, input);
				FrameProfiler::GetInstance().AddBLASBuild(
					static_cast<uint32_t>(geometries.size()));
				work.requireTlasRebuild = true;
				entry.consecutiveRefitCount = 0;
			} else if (entry.poseGeneration != skinnedSource.poseGeneration ||
				entry.bufferGeneration != skinnedSource.bufferGeneration ||
				entry.geometryLayoutHash != geometryLayoutHash ||
				entry.vertexAddress != vertexAddress) {

				if (kMaxConsecutiveBLASRefits <=
					entry.consecutiveRefitCount + 1) {

					entry.blas.Rebuild(work.commandList, input);
					entry.consecutiveRefitCount = 0;
					FrameProfiler::GetInstance().AddBLASBuild(
						static_cast<uint32_t>(geometries.size()));
				} else {

					entry.blas.Update(work.commandList, input);
					++entry.consecutiveRefitCount;
					FrameProfiler::GetInstance().AddBLASRefit(
						static_cast<uint32_t>(geometries.size()));
				}
			} else {

				FrameProfiler::GetInstance().AddBLASSkip(
					static_cast<uint32_t>(geometries.size()));
			}
			entry.poseGeneration = skinnedSource.poseGeneration;
			entry.bufferGeneration = skinnedSource.bufferGeneration;
			entry.geometryLayoutHash = geometryLayoutHash;
			entry.vertexAddress = vertexAddress;
			blasResource = entry.blas.GetResource();
		} else if (usesInstanceBLAS) {

			StaticInstanceBLASEntry& entry = *staticInstanceEntry;
			std::vector<RaytracingBLASGeometryInput> lodGeometries =
				buildLODGeometries(selectedLOD);
			RaytracingBLASInput lodInput{};
			lodInput.geometries = lodGeometries;
			lodInput.allowUpdate = true;

			BottomLevelAccelerationStructure& blas =
				entry.lodBLASes[selectedLOD];
			const bool geometryChanged =
				entry.lodGeometryLayoutHashes[selectedLOD] !=
				geometryLayoutHash;
			if (!blas.IsBuilt()) {

				blas.SetRetirementQueue(work.graphicsCore.GetDXObject().GetResourceRetirement());
				blas.Build(work.device, work.commandList, lodInput);
				FrameProfiler::GetInstance().AddBLASBuild(
					static_cast<uint32_t>(lodGeometries.size()));
				work.requireTlasRebuild = true;
			} else if (geometryChanged) {

				blas.Update(work.commandList, lodInput);
				FrameProfiler::GetInstance().AddBLASRefit(
					static_cast<uint32_t>(lodGeometries.size()));
			} else {

				FrameProfiler::GetInstance().AddBLASSkip(
					static_cast<uint32_t>(lodGeometries.size()));
			}
			entry.lodGeometryLayoutHashes[selectedLOD] =
				geometryLayoutHash;
			blasResource = blas.GetResource();
			entry.geometryLayoutHash = geometryLayoutHash;
		} else {

			const uint32_t blasLODCount =
				meshResource->isSkinned ? 1 :
				kMeshLODCount;
			for (uint32_t lodIndex = 0;
				lodIndex < blasLODCount; ++lodIndex) {

				BLASKey key{};
				key.meshAssetID = src.meshAssetID;
				key.reloadGeneration = reloadGeneration;
				key.lodIndex = lodIndex;
				key.geometryLayoutHash =
					geometryLayoutHash;

				auto blasIt = blasCache_.blases_.find(key);
				if (blasIt != blasCache_.blases_.end() &&
					blasIt->second.IsBuilt()) {

					FrameProfiler::GetInstance().
						AddBLASSkip(
							static_cast<uint32_t>(
								geometries.size()));
					if (lodIndex == selectedLOD) {
						blasResource =
							blasIt->second.GetResource();
					}
					continue;
				}

				std::vector<RaytracingBLASGeometryInput> lodGeometries =
					buildLODGeometries(lodIndex);

				RaytracingBLASInput lodInput{};
				lodInput.geometries = lodGeometries;
				lodInput.allowUpdate = false;

				BottomLevelAccelerationStructure& blas =
					blasCache_.blases_[key];
				blas.SetRetirementQueue(work.graphicsCore.GetDXObject().GetResourceRetirement());
				blas.Build(work.device, work.commandList, lodInput);
				FrameProfiler::GetInstance().
					AddBLASBuild(
						static_cast<uint32_t>(
							lodGeometries.size()));
				work.requireTlasRebuild = true;
				if (lodIndex == selectedLOD) {
					blasResource = blas.GetResource();
				}
			}
		}
		if (!blasResource) {
			continue;
		}

		RaytracingInstanceShaderData instanceShaderData{};
		instanceShaderData.vertexDescriptorIndex = vertexDescriptorIndex;
		instanceShaderData.indexDescriptorIndex = meshResource->indexSRV.srvIndex;
		instanceShaderData.vertexOffset = vertexOffset;
		instanceShaderData.geometryDataOffset = geometryDataOffset;
		instanceShaderData.renderFlags = ToRaytracingRenderFlags(
			src.renderer ? src.renderer->renderFlags :
				MeshRenderFlags::Default);
		const uint32_t shaderInstanceIndex =
			static_cast<uint32_t>(result_.sceneInstanceScratch_.size());
		result_.sceneInstanceScratch_.emplace_back(instanceShaderData);
		result_.scenePickRecordOffsets_.emplace_back(pickRecordOffset);

		RaytracingTLASInstance instance{};
		instance.blas = blasResource;
		instance.instanceID = shaderInstanceIndex;
		instance.hitGroupIndex = 0;
		instance.mask = kRaytracingMaskAlwaysHit;
		if (src.renderer) {
			if (src.castShadows) {
				instance.mask |= kRaytracingMaskShadowCaster;
			}
			if (HasMeshRenderFlag(src.renderer->renderFlags, MeshRenderFlags::CastReflection)) {
				instance.mask |= kRaytracingMaskReflectionCaster;
			}
		} else {

			instance.mask |= kRaytracingMaskShadowCaster |
				kRaytracingMaskReflectionCaster;
		}
		instance.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance.worldMatrix = src.worldMatrix;
		const uint32_t tlasInstanceIndex =
			static_cast<uint32_t>(work.tlasInstances.size());
		work.tlasInstances.emplace_back(instance);
		work.tlasEntityKeys.emplace_back(
			SceneEntityKey{
				.world = src.world,
				.entity = src.entity,
			});
		if (!meshResource->isSkinned) {

			CachedMeshLODInstance lodInstance{};
			lodInstance.meshAssetID = src.meshAssetID;
			lodInstance.world = src.world;
			lodInstance.entity = src.entity;
			lodInstance.reloadGeneration = reloadGeneration;
			lodInstance.geometryLayoutHash =
				geometryLayoutHash;
			lodInstance.tracksInstanceLayout =
				hasCustomGeometryTransforms;
			lodInstance.usesInstanceBLAS =
				usesInstanceBLAS;
			lodInstance.tlasInstanceIndex =
				tlasInstanceIndex;
			lodInstance.geometryDataOffset =
				geometryDataOffset;
			lodInstance.geometryCount =
				static_cast<uint32_t>(
					meshResource->subMeshes.size());
			lodInstance.lodIndex = selectedLOD;
			lodInstance.worldBoundsCenter =
				worldBoundsCenter;
			lodInstance.worldBoundsRadius =
				worldBoundsRadius;
			work.meshLODRecordIndices.emplace_back(
				static_cast<uint32_t>(
					work.meshLODInstances.size()));
			work.meshLODInstances.emplace_back(
				lodInstance);
		} else {
			work.meshLODRecordIndices.emplace_back(UINT32_MAX);
		}
	}
}
