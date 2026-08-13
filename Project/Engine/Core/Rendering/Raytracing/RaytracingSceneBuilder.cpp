#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Renderer/Pipeline/RenderPipelineRunner.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/RenderBillboardUtility.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveGeometryManager.h>
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>

#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Rendering/DxObject/Core/BufferUploadService.h>
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
namespace {

	template <typename T>
	void CreateImmutableSRV(ID3D12Device* device, Engine::BufferUploadService& uploadService,
		Engine::SRVDescriptor& srvDescriptor, Engine::MeshStructuredHandle<T>& out,
		const std::vector<T>& data, const wchar_t* debugName) {

		if (data.empty()) {
			return;
		}

		out.buffer = std::make_unique<Engine::DxImmutableStructuredBuffer<T>>();
		out.buffer->Create(device, uploadService, std::span<const T>(data.data(), data.size()));
		if (ID3D12Resource* resource = out.buffer->GetResource()) {
			resource->SetName(debugName);
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = out.buffer->GetSRVDesc();
		srvDescriptor.CreateSRV(out.srvIndex, out.buffer->GetResource(), srvDesc);
		out.srvGPUHandle = srvDescriptor.GetGPUHandle(out.srvIndex);
	}

	bool HasValidFillMeshIndices(
		std::span<const Engine::FillMeshTriangleIndex> indices,
		size_t vertexCount) {

		if (indices.empty() || indices.size() % 3 != 0) {
			return false;
		}
		for (const Engine::FillMeshTriangleIndex& index : indices) {
			if (vertexCount <= index.value) {
				return false;
			}
		}
		return true;
	}

	// テクスチャを持たない単色サブメッシュのシェーダーデータ、FillMesh/Primitiveで共用する
	Engine::MeshSubMeshShaderData MakeFlatSubMeshData(const Engine::Color4& baseColor) {

		Engine::MeshSubMeshShaderData subMeshData{};
		subMeshData.importedBaseColor = baseColor;
		subMeshData.baseColorTextureIndex = UINT32_MAX;
		subMeshData.normalTextureIndex = UINT32_MAX;
		subMeshData.metallicRoughnessTextureIndex = UINT32_MAX;
		subMeshData.emissiveTextureIndex = UINT32_MAX;
		subMeshData.occlusionTextureIndex = UINT32_MAX;
		subMeshData.specularTextureIndex = UINT32_MAX;
		subMeshData.localMatrix = Engine::Matrix4x4::Identity();
		subMeshData.localNormalMatrix = Engine::Matrix4x4::Identity();
		subMeshData.color = Engine::Color4::White();
		subMeshData.emissiveColor = Engine::Color4(0.0f, 0.0f, 0.0f, 0.0f);
		subMeshData.uvMatrix = Engine::Matrix4x4::Identity();
		subMeshData.roughness = 1.0f;
		return subMeshData;
	}

	uint64_t ComputeGeometryLayoutHash(
		std::span<const Engine::SubMeshMaterial> subMeshes,
		uint32_t geometryCount) {

		uint64_t hash = geometryCount;
		for (uint32_t index = 0; index < geometryCount; ++index) {

			const Engine::Matrix4x4 localMatrix =
				index < subMeshes.size() ?
				Engine::MeshSubMeshRuntime::BuildRenderLocalMatrix(subMeshes[index]) :
				Engine::Matrix4x4::Identity();
			for (uint32_t row = 0; row < 4; ++row) {
				for (uint32_t column = 0; column < 4; ++column) {

					Engine::Algorithm::HashCombine(hash,
						std::bit_cast<uint32_t>(localMatrix.m[row][column]));
				}
			}
		}
		return hash;
	}

	// 大量の配置変更ではrefit後のBVH品質が落ちるためTLASを再構築する
	bool RequiresTLASRebuildForTraceQuality(
		size_t instanceCount, uint32_t changedInstanceCount) {

		return 256 <= changedInstanceCount &&
			instanceCount <=
				static_cast<size_t>(changedInstanceCount) * 4;
	}

	float GetMatrixMaxScale(const Engine::Matrix4x4& matrix) {

		const float scaleX = std::sqrt(
			matrix.m[0][0] * matrix.m[0][0] +
			matrix.m[0][1] * matrix.m[0][1] +
			matrix.m[0][2] * matrix.m[0][2]);
		const float scaleY = std::sqrt(
			matrix.m[1][0] * matrix.m[1][0] +
			matrix.m[1][1] * matrix.m[1][1] +
			matrix.m[1][2] * matrix.m[1][2]);
		const float scaleZ = std::sqrt(
			matrix.m[2][0] * matrix.m[2][0] +
			matrix.m[2][1] * matrix.m[2][1] +
			matrix.m[2][2] * matrix.m[2][2]);
		return (std::max)(scaleX, (std::max)(scaleY, scaleZ));
	}

	void EncapsulateSphere(const Engine::Vector3& sourceCenter,
		float sourceRadius, Engine::Vector3& center, float& radius) {

		const Engine::Vector3 difference = sourceCenter - center;
		const float distance = difference.Length();
		if (distance + sourceRadius <= radius) {
			return;
		}
		if (distance + radius <= sourceRadius) {
			center = sourceCenter;
			radius = sourceRadius;
			return;
		}

		const float newRadius =
			(distance + radius + sourceRadius) * 0.5f;
		if (0.00001f < distance) {
			center += difference *
				((newRadius - radius) / distance);
		}
		radius = newRadius;
	}

	void CalculateMeshWorldBounds(
		const Engine::MeshGPUResource& meshResource,
		std::span<const Engine::SubMeshMaterial> subMeshes,
		const Engine::Matrix4x4& worldMatrix,
		Engine::Vector3& outCenter, float& outRadius) {

		outCenter = Engine::Vector3::Transform(
			meshResource.boundsCenter, worldMatrix);
		outRadius = meshResource.boundsRadius *
			GetMatrixMaxScale(worldMatrix);
		if (subMeshes.empty()) {
			return;
		}

		bool initialized = false;
		for (const Engine::SubMeshMaterial& subMesh : subMeshes) {

			const Engine::Matrix4x4 localMatrix =
				Engine::MeshSubMeshRuntime::
					BuildRenderLocalMatrix(subMesh);
			const Engine::Vector3 localCenter =
				Engine::Vector3::Transform(
					meshResource.boundsCenter, localMatrix);
			const float localRadius =
				meshResource.boundsRadius *
				GetMatrixMaxScale(localMatrix);
			const Engine::Vector3 worldCenter =
				Engine::Vector3::Transform(
					localCenter, worldMatrix);
			const float worldRadius =
				localRadius * GetMatrixMaxScale(worldMatrix);
			if (!initialized) {
				outCenter = worldCenter;
				outRadius = worldRadius;
				initialized = true;
				continue;
			}
			EncapsulateSphere(
				worldCenter, worldRadius, outCenter, outRadius);
		}
	}

	const Engine::MeshLODRange& ResolveRaytracingLODRange(
		const Engine::SubMeshDesc& subMesh, uint32_t lodIndex) {

		uint32_t resolvedLOD = (std::min)(
			lodIndex, Engine::kMeshLODCount - 1);
		while (0 < resolvedLOD &&
			subMesh.lods[resolvedLOD].indexCount < 3) {
			--resolvedLOD;
		}
		return subMesh.lods[resolvedLOD];
	}

	uint32_t ResolveMeshLOD(
		const Engine::GraphicsRuntimeFeatures& features,
		const Engine::ResolvedRenderView* cullingView,
		const Engine::Vector3& center, float radius) {

		if (!features.useMeshLOD || !cullingView) {
			return 0;
		}
		const Engine::ResolvedCameraView* camera =
			cullingView->FindCamera(
				Engine::RenderCameraDomain::Perspective);
		if (!camera || !camera->valid) {
			return 0;
		}

		const Engine::Vector3 viewCenter =
			Engine::Vector3::Transform(
				center, camera->matrices.viewMatrix);
		const float nearZ = viewCenter.z - radius;
		if (nearZ <= (std::max)(camera->nearClip, 0.00001f)) {
			return 0;
		}

		const float projectionX = std::abs(
			camera->matrices.projectionMatrix.m[0][0]);
		const float projectionY = std::abs(
			camera->matrices.projectionMatrix.m[1][1]);
		const float pixelRadiusX =
			std::abs(radius * projectionX / nearZ) *
			static_cast<float>((std::max)(cullingView->width, 1u)) *
			0.5f;
		const float pixelRadiusY =
			std::abs(radius * projectionY / nearZ) *
			static_cast<float>((std::max)(cullingView->height, 1u)) *
			0.5f;
		const float pixelRadius =
			(std::max)(pixelRadiusX, pixelRadiusY);
		if (features.meshLOD0PixelThreshold <= pixelRadius) {
			return 0;
		}
		if (features.meshLOD1PixelThreshold <= pixelRadius) {
			return 1;
		}
		if (features.meshLOD2PixelThreshold <= pixelRadius) {
			return 2;
		}
		return Engine::kMeshLODCount - 1;
	}

	uint64_t ComputeLODViewHash(
		const Engine::GraphicsRuntimeFeatures& features,
		const Engine::ResolvedRenderView* cullingView) {

		uint64_t hash = features.useMeshLOD ? 1ull : 0ull;
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				features.meshLOD0PixelThreshold));
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				features.meshLOD1PixelThreshold));
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				features.meshLOD2PixelThreshold));
		if (!cullingView) {
			return hash;
		}

		Engine::Algorithm::HashCombine(hash, cullingView->width);
		Engine::Algorithm::HashCombine(hash, cullingView->height);
		const Engine::ResolvedCameraView* camera =
			cullingView->FindCamera(
				Engine::RenderCameraDomain::Perspective);
		if (!camera || !camera->valid) {
			return hash;
		}
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(camera->nearClip));
		for (uint32_t row = 0; row < 4; ++row) {
			for (uint32_t column = 0; column < 4; ++column) {

				Engine::Algorithm::HashCombine(hash,
					std::bit_cast<uint32_t>(
						camera->matrices.viewMatrix.
							m[row][column]));
			}
		}
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				camera->matrices.projectionMatrix.m[0][0]));
		Engine::Algorithm::HashCombine(hash,
			std::bit_cast<uint32_t>(
				camera->matrices.projectionMatrix.m[1][1]));
		return hash;
	}
}

//============================================================================
//	RaytracingSceneBuilder classMethods
//============================================================================
void Engine::RaytracingSceneBuilder::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	// バッファ初期化
	sceneInstances_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	sceneGeometries_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	sceneSubMeshes_.Init(graphicsCore.GetDXObject().GetDevice(), &graphicsCore.GetSRVDescriptor());
	srvDescriptor_ = &graphicsCore.GetSRVDescriptor();
	// 事前にある程度の容量を確保しておく
	sceneInstances_.EnsureCapacity(256);
	sceneGeometries_.EnsureCapacity(256);
	sceneSubMeshes_.EnsureCapacity(256);
	sceneInstanceScratch_.reserve(256);
	sceneGeometryScratch_.reserve(256);
	sceneSubMeshScratch_.reserve(256);

	firstTLASBuild_ = true;
	tlasInstanceHash_ = 0;
	initialized_ = true;
}

void Engine::RaytracingSceneBuilder::Finalize() {

	if (!initialized_) {
		return;
	}

	sceneInstances_.Release();
	sceneGeometries_.Release();
	sceneSubMeshes_.Release();
	sceneInstanceScratch_.clear();
	sceneGeometryScratch_.clear();
	sceneSubMeshScratch_.clear();
	scenePickRecords_.clear();
	scenePickRecordOffsets_.clear();
	cachedTLASInstances_.clear();
	cachedTLASInstanceIndices_.clear();
	cachedMeshLODInstances_.clear();
	cachedMeshLODRecordIndices_.clear();

	textureKeyCache_.clear();
	textureDescriptorIndexCache_.clear();

	blases_.clear();
	staticInstanceBLASes_.clear();
	dynamicBlases_.clear();
	for (auto& pair : fillMeshRTResources_) {
		pair.second.Release(srvDescriptor_);
	}
	fillMeshRTResources_.clear();
	meshBlasGeneration_.clear();
	srvDescriptor_ = nullptr;
	firstTLASBuild_ = true;
	tlasInstanceHash_ = 0;
	initialized_ = false;
	builtThisFrame_ = false;
	builtSceneInstanceID_ = {};
	cachedStaticScene_ = false;
	cachedSceneInstanceID_ = {};
	cachedRenderRevision_ = 0;
	cachedTransformRevision_ = 0;
	cachedMeshResourceRevision_ = 0;
	cachedLODViewHash_ = 0;
	cachedBLASGeometryCount_ = 0;
	cachedTLASInstanceCount_ = 0;
	sceneUploadFrameSerials_ = { 0, 0, 0 };
}

void Engine::RaytracingSceneBuilder::BeginFrame(GraphicsCore& graphicsCore) {

	if (!initialized_) {
		Init(graphicsCore);
	}

	// フラグリセット
	builtThisFrame_ = false;
	builtSceneInstanceID_ = {};

	const uint64_t currentFrame =
		GraphicsFrameState::GetFrameSerial();
	auto expired = [currentFrame](uint64_t lastUsedFrame) {
		return currentFrame >
			lastUsedFrame + kGraphicsFrameContextCount;
	};

	// 削除済みEntityの動的BLASはGPU参照が切れる3フレーム後に破棄する
	std::erase_if(dynamicBlases_, [&](const auto& pair) {
		return expired(pair.second.lastUsedFrame);
	});
	std::erase_if(staticInstanceBLASes_, [&](const auto& pair) {
		return expired(pair.second.lastUsedFrame);
	});

	for (auto it = fillMeshRTResources_.begin();
		it != fillMeshRTResources_.end();) {

		if (expired(it->second.lastUsedFrame)) {
			it->second.Release(srvDescriptor_);
			it = fillMeshRTResources_.erase(it);
		} else {
			++it;
		}
	}
}

void Engine::RaytracingSceneBuilder::BuildForScene(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, MeshRenderBackend* meshBackend, PrimitiveGeometryManager* primitiveGeometryManager,
	const RenderSceneBatch& renderBatch, SceneExecutionContext& context) {

	const auto& featureController = graphicsCore.GetDXObject().GetFeatureController();
	const bool canBuildRaytracingScene = featureController.GetSupport().SupportsRayTracingPath() &&
		graphicsCore.GetDXObject().ShouldBuildRaytracingScene();
	if (!canBuildRaytracingScene) {
		return;
	}
	if (!context.sceneInstance) {
		return;
	}

	// すでに同一シーンインスタンスで構築している場合は、構築済みのシーン情報を渡す
	if (builtThisFrame_ && builtSceneInstanceID_ == context.sceneInstance->instanceID) {
		PublishBuiltScene(context);
		return;
	}

	const uint64_t meshResourceRevision =
		meshBackend ? meshBackend->GetMeshResourceRevision() : 0;
	const GraphicsRuntimeFeatures& runtimeFeatures =
		featureController.GetRuntimeFeatures();
	const ResolvedRenderView* lodView =
		context.cullingView ? context.cullingView : context.view;
	const uint64_t lodViewHash =
		ComputeLODViewHash(runtimeFeatures, lodView);
	bool lodResourceMissing = false;
	auto updateCachedLODSelections = [&]() {

		uint32_t changedCount = 0;
		if (!meshBackend) {
			return changedCount;
		}
		for (CachedMeshLODInstance& record :
			cachedMeshLODInstances_) {

			const uint32_t lodIndex = ResolveMeshLOD(
				runtimeFeatures, lodView,
				record.worldBoundsCenter,
				record.worldBoundsRadius);
			if (lodIndex == record.lodIndex) {
				continue;
			}

			const MeshGPUResource* meshResource =
				meshBackend->FindMeshResource(
					record.meshAssetID);
			if (!meshResource ||
				record.tlasInstanceIndex >=
					cachedTLASInstances_.size()) {
				continue;
			}

			ID3D12Resource* blasResource = nullptr;
			if (record.usesInstanceBLAS) {

				StaticInstanceBLASKey key{};
				key.world = record.world;
				key.entity = record.entity;
				key.meshAssetID = record.meshAssetID;
				key.reloadGeneration = record.reloadGeneration;
				auto blasIt = staticInstanceBLASes_.find(key);
				if (blasIt == staticInstanceBLASes_.end() ||
					blasIt->second.lodGeometryLayoutHashes[lodIndex] !=
						record.geometryLayoutHash ||
					!blasIt->second.lodBLASes[lodIndex].IsBuilt()) {
					lodResourceMissing = true;
					continue;
				}
				blasResource = blasIt->second.
					lodBLASes[lodIndex].GetResource();
			} else {

				BLASKey key{};
				key.meshAssetID = record.meshAssetID;
				key.reloadGeneration = record.reloadGeneration;
				key.lodIndex = lodIndex;
				key.geometryLayoutHash =
					record.geometryLayoutHash;
				auto blasIt = blases_.find(key);
				if (blasIt == blases_.end() ||
					!blasIt->second.IsBuilt()) {
					continue;
				}
				blasResource = blasIt->second.GetResource();
			}

			cachedTLASInstances_[
				record.tlasInstanceIndex].blas =
					blasResource;
			const uint32_t geometryCount = (std::min)(
				record.geometryCount,
				static_cast<uint32_t>(
					meshResource->subMeshes.size()));
			for (uint32_t geometryIndex = 0;
				geometryIndex < geometryCount;
				++geometryIndex) {

				const uint32_t dataIndex =
					record.geometryDataOffset +
					geometryIndex;
				if (sceneGeometryScratch_.size() <=
					dataIndex) {
					break;
				}
				sceneGeometryScratch_[dataIndex].
					indexOffset =
					ResolveRaytracingLODRange(
						meshResource->subMeshes[
							geometryIndex],
						lodIndex).indexOffset;
			}
			record.lodIndex = lodIndex;
			++changedCount;
		}
		return changedCount;
	};
	const bool matchesStaticScene =
		cachedStaticScene_ &&
		cachedSceneInstanceID_ == context.sceneInstance->instanceID &&
		cachedRenderRevision_ ==
			renderBatch.GetSourceRenderRevision() &&
		cachedMeshResourceRevision_ == meshResourceRevision &&
		tlas_.IsBuilt();
	if (matchesStaticScene) {

		const uint64_t currentFrame =
			GraphicsFrameState::GetFrameSerial();
		for (const CachedMeshLODInstance& record :
			cachedMeshLODInstances_) {

			if (!record.tracksInstanceLayout) {
				continue;
			}
			StaticInstanceBLASKey key{};
			key.world = record.world;
			key.entity = record.entity;
			key.meshAssetID = record.meshAssetID;
			key.reloadGeneration = record.reloadGeneration;
			auto entry = staticInstanceBLASes_.find(key);
			if (entry != staticInstanceBLASes_.end()) {
				entry->second.lastUsedFrame = currentFrame;
			}
		}
	}
	if (matchesStaticScene &&
		cachedTransformRevision_ ==
			renderBatch.GetSourceTransformRevision()) {

		const uint32_t lodChangedCount =
			cachedLODViewHash_ != lodViewHash ?
				updateCachedLODSelections() : 0;
		if (lodResourceMissing) {
			cachedStaticScene_ = false;
		}
		if (0 < lodChangedCount) {

			tlas_.Update(
				graphicsCore.GetDXObject().GetDxCommand()->
					GetCommandList(),
				cachedTLASInstances_);
			tlasInstanceHash_ =
				ComputeTLASInstanceHash(
					cachedTLASInstances_);
			FrameProfiler::GetInstance().AddTLASRefit();
		} else {
			FrameProfiler::GetInstance().AddTLASSkip();
		}
		cachedLODViewHash_ = lodViewHash;
		UploadCachedSceneBuffers();
		FrameProfiler::GetInstance().AddBLASSkip(cachedBLASGeometryCount_);
		FrameProfiler::GetInstance().SetTLASInstanceCount(cachedTLASInstanceCount_);
		builtThisFrame_ = true;
		builtSceneInstanceID_ = context.sceneInstance->instanceID;
		PublishBuiltScene(context);
		return;
	}
	if (matchesStaticScene &&
		renderBatch.HasCompleteTransformChanges() &&
		!cachedTLASInstances_.empty()) {

		bool transformChanged = false;
		uint32_t changedInstanceCount = 0;
		for (const RenderTransformChange& change :
			renderBatch.GetTransformChanges()) {
			SceneEntityKey key{};
			key.world = change.world;
			key.entity = change.entity;
			const auto [begin, end] =
				cachedTLASInstanceIndices_.equal_range(key);
			for (auto it = begin; it != end; ++it) {
				RaytracingTLASInstance& instance =
					cachedTLASInstances_[it->second];
				if (instance.worldMatrix ==
					change.worldMatrix) {
					continue;
				}
				instance.worldMatrix =
					change.worldMatrix;
				if (it->second <
					cachedMeshLODRecordIndices_.size()) {

					const uint32_t recordIndex =
						cachedMeshLODRecordIndices_[
							it->second];
					if (recordIndex != UINT32_MAX &&
						recordIndex <
							cachedMeshLODInstances_.size()) {

						CachedMeshLODInstance& record =
							cachedMeshLODInstances_[
								recordIndex];
						record.lodIndex =
							Engine::kMeshLODCount;
						const MeshGPUResource* meshResource =
							meshBackend ?
								meshBackend->
									FindMeshResource(
										record.meshAssetID) :
								nullptr;
						if (meshResource) {

							const std::span<
								const SubMeshMaterial>
								subMeshes =
									change.world ?
										GetMeshSubMeshes(
											*change.world,
											change.entity) :
										std::span<
											const SubMeshMaterial>{};
							CalculateMeshWorldBounds(
								*meshResource,
								subMeshes,
								change.worldMatrix,
								record.worldBoundsCenter,
								record.worldBoundsRadius);
						}
					}
				}
				transformChanged = true;
				++changedInstanceCount;
			}
		}

		const uint32_t lodChangedCount =
			(transformChanged ||
				cachedLODViewHash_ != lodViewHash) ?
				updateCachedLODSelections() : 0;
		if (lodResourceMissing) {
			cachedStaticScene_ = false;
		}
		changedInstanceCount += lodChangedCount;
		if (transformChanged || 0 < lodChangedCount) {
			ID3D12GraphicsCommandList6* commandList =
				graphicsCore.GetDXObject().GetDxCommand()->
				GetCommandList();
			const bool rebuildForTraceQuality =
				RequiresTLASRebuildForTraceQuality(
					cachedTLASInstances_.size(),
					changedInstanceCount);
			if (rebuildForTraceQuality) {

				tlas_.Build(
					graphicsCore.GetDXObject().GetDevice(),
					commandList, cachedTLASInstances_, true);
				FrameProfiler::GetInstance().AddTLASBuild();
			} else {

				tlas_.Update(
					commandList, cachedTLASInstances_);
				FrameProfiler::GetInstance().AddTLASRefit();
			}
		} else {
			FrameProfiler::GetInstance().AddTLASSkip();
		}

		tlasInstanceHash_ =
			ComputeTLASInstanceHash(
				cachedTLASInstances_);
		cachedTransformRevision_ =
			renderBatch.GetSourceTransformRevision();
		cachedLODViewHash_ = lodViewHash;
		UploadCachedSceneBuffers();
		FrameProfiler::GetInstance().AddBLASSkip(
			cachedBLASGeometryCount_);
		FrameProfiler::GetInstance().SetTLASInstanceCount(
			cachedTLASInstanceCount_);
		builtThisFrame_ = true;
		builtSceneInstanceID_ =
			context.sceneInstance->instanceID;
		PublishBuiltScene(context);
		return;
	}
	cachedStaticScene_ = false;

	scenePickRecords_.clear();
	scenePickRecordOffsets_.clear();

	// シーン内の可視メッシュインスタンスを収集する
	std::vector<CollectedMeshInstance> sceneMeshes;
	if (meshBackend) {
		CollectSceneMeshInstances(renderBatch, context, sceneMeshes);
	}
	std::vector<CollectedFillMeshInstance> sceneFillMeshes;
	CollectSceneFillMeshInstances(renderBatch, context, sceneFillMeshes);
	std::vector<CollectedPrimitiveInstance> scenePrimitives;
	if (primitiveGeometryManager) {
		CollectScenePrimitiveInstances(renderBatch, context, scenePrimitives);
	}
	if (sceneMeshes.empty() && sceneFillMeshes.empty() && scenePrimitives.empty()) {
		return;
	}

	// 必要メッシュを一度だけ要求
	std::unordered_set<AssetID> requiredMeshSet{};
	requiredMeshSet.reserve(sceneMeshes.size());
	for (const CollectedMeshInstance& instance : sceneMeshes) {
		if (instance.meshAssetID) {
			requiredMeshSet.insert(instance.meshAssetID);
		}
	}

	// メッシュリソースを要求
	std::vector<AssetID> requiredMeshes{};
	requiredMeshes.reserve(requiredMeshSet.size());
	for (const AssetID& meshAssetID : requiredMeshSet) {

		requiredMeshes.emplace_back(meshAssetID);
	}
	if (meshBackend && !requiredMeshes.empty()) {
		meshBackend->RequestMeshes(graphicsCore, assetDatabase, requiredMeshes);
	}

	ID3D12Device8* device = graphicsCore.GetDXObject().GetDevice();
	ID3D12GraphicsCommandList6* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
	BufferUploadService& uploadService = graphicsCore.GetBufferUploadService();

	// データクリア
	sceneInstanceScratch_.clear();
	sceneGeometryScratch_.clear();
	sceneSubMeshScratch_.clear();

	// BLASの構築とTLASインスタンスの準備
	std::vector<RaytracingTLASInstance> tlasInstances;
	tlasInstances.reserve(sceneMeshes.size() + sceneFillMeshes.size());
	std::vector<SceneEntityKey> tlasEntityKeys;
	tlasEntityKeys.reserve(sceneMeshes.size() +
		sceneFillMeshes.size() + scenePrimitives.size());
	std::vector<CachedMeshLODInstance> meshLODInstances;
	meshLODInstances.reserve(sceneMeshes.size());
	std::vector<uint32_t> meshLODRecordIndices;
	meshLODRecordIndices.reserve(sceneMeshes.size() +
		sceneFillMeshes.size() + scenePrimitives.size());

	// BLASリソースを新規/作り直しした場合はTLASのrefitでは反映できないため完全再構築する
	bool requireTlasRebuild = false;
	bool requireTlasRefit = false;
	bool staticScene = sceneFillMeshes.empty();
	uint32_t blasGeometryCount = 0;

	for (const CollectedMeshInstance& src : sceneMeshes) {

		// メッシュリソースを取得
		const MeshGPUResource* meshResource = meshBackend->FindMeshResource(src.meshAssetID);
		if (!meshResource) {
			continue;
		}
		if (!meshResource->vertexSRV.buffer || !meshResource->indexSRV.buffer) {
			continue;
		}
		if (meshResource->subMeshes.empty()) {
			continue;
		}
		staticScene = staticScene && !meshResource->isSkinned;
		blasGeometryCount += static_cast<uint32_t>(meshResource->subMeshes.size());
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
				staticInstanceBLASes_[staticInstanceKey];
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
			ResolveMeshLOD(runtimeFeatures, lodView,
				worldBoundsCenter, worldBoundsRadius);

		SkinnedVertexSource skinnedSource{};

		// スキンメッシュの頂点ソースを持っているか
		// リソース情報をアウトプットする
		bool hasSkinnedSource = meshResource->isSkinned && meshBackend->FindSkinnedVertexSource(
			src.world, src.entity, src.meshAssetID, skinnedSource);

		// ホットリロードで世代が変わったら、このメッシュの旧世代BLASを破棄してから作り直す
		const uint32_t reloadGeneration = meshResource->reloadGeneration;
		auto generationIt = meshBlasGeneration_.find(src.meshAssetID);
		if (generationIt != meshBlasGeneration_.end() && generationIt->second != reloadGeneration) {

			std::erase_if(blases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
				});
			std::erase_if(dynamicBlases_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID && pair.first.reloadGeneration != reloadGeneration;
				});
			std::erase_if(staticInstanceBLASes_, [&](const auto& pair) {
				return pair.first.meshAssetID == src.meshAssetID &&
					pair.first.reloadGeneration != reloadGeneration;
				});
		}
		meshBlasGeneration_[src.meshAssetID] = reloadGeneration;

		// 1メッシュの全サブメッシュを1つのBLASへまとめる
		std::vector<RaytracingBLASGeometryInput> geometries{};
		geometries.reserve(meshResource->subMeshes.size());

		const uint32_t geometryDataOffset =
			static_cast<uint32_t>(sceneGeometryScratch_.size());
		const uint32_t pickRecordOffset =
			static_cast<uint32_t>(scenePickRecords_.size());

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
				static_cast<uint32_t>(sceneSubMeshScratch_.size());

			// サブメッシュデータを構築
			MeshSubMeshShaderData subMeshData{};
			subMeshData.importedBaseColor = importedSubMesh.baseColor;
			AssetID baseColorTextureAsset =
				MeshDrawPathCommon::ResolveSubMeshBaseColorTextureAssetID(
					*meshResource, subMeshes, subMeshIndex);
			if (baseColorTextureAsset) {

				subMeshData.baseColorTextureIndex = ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, baseColorTextureAsset);
			} else if (MeshDrawPathCommon::WasSubMeshBaseColorTextureAssigned(
				*meshResource, subMeshes, subMeshIndex)) {

				// 宣言はあるが見つからない:エラーテクスチャ
				subMeshData.baseColorTextureIndex = ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, AssetID{});
			} else {

				// テクスチャ未設定:シェーダ側でimportedBaseColor*colorを使う
				subMeshData.baseColorTextureIndex = UINT32_MAX;
			}

			AssetID normalAsset = MeshDrawPathCommon::ResolveSubMeshNormalTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			AssetID metallicRoughnessAsset =
				MeshDrawPathCommon::ResolveSubMeshMetallicRoughnessTextureAssetID(
					*meshResource, subMeshes, subMeshIndex);
			AssetID emissiveAsset = MeshDrawPathCommon::ResolveSubMeshEmissiveTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			AssetID occlusionAsset = MeshDrawPathCommon::ResolveSubMeshOcclusionTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			AssetID specularAsset = MeshDrawPathCommon::ResolveSubMeshSpecularTextureAssetID(
				*meshResource, subMeshes, subMeshIndex);
			subMeshData.normalTextureIndex = normalAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, normalAsset) : UINT32_MAX;
			subMeshData.metallicRoughnessTextureIndex = metallicRoughnessAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, metallicRoughnessAsset) : UINT32_MAX;
			subMeshData.emissiveTextureIndex = emissiveAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, emissiveAsset) : UINT32_MAX;
			subMeshData.occlusionTextureIndex = occlusionAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, occlusionAsset) : UINT32_MAX;
			subMeshData.specularTextureIndex = specularAsset ?
				ResolveTextureDescriptorIndex(graphicsCore, assetDatabase, specularAsset) : UINT32_MAX;

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
			sceneSubMeshScratch_.emplace_back(subMeshData);

			// メッシュピック用のサブメッシュ情報を追加
			MeshSubMeshPickRecord pickRecord{};
			pickRecord.entity = src.entity;
			pickRecord.subMeshIndex = subMeshIndex;
			if (hasMesh) {

				pickRecord.subMeshStableID = subMeshes[subMeshIndex].stableID;
			}
			scenePickRecords_.emplace_back(pickRecord);

			RaytracingGeometryShaderData geometryData{};
			geometryData.subMeshDataIndex = subMeshDataIndex;
			geometryData.indexOffset =
				selectedRange.indexOffset;
			geometryData.pickRecordIndex =
				static_cast<uint32_t>(scenePickRecords_.size() - 1);
			sceneGeometryScratch_.emplace_back(geometryData);
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

			DynamicBLASEntry& entry = dynamicBlases_[key];
			entry.lastUsedFrame =
				GraphicsFrameState::GetFrameSerial();
			if (!entry.blas.IsBuilt()) {

				entry.blas.Build(device, commandList, input);
				FrameProfiler::GetInstance().AddBLASBuild(
					static_cast<uint32_t>(geometries.size()));
				requireTlasRebuild = true;
			} else if (entry.poseGeneration != skinnedSource.poseGeneration ||
				entry.bufferGeneration != skinnedSource.bufferGeneration ||
				entry.geometryLayoutHash != geometryLayoutHash ||
				entry.vertexAddress != vertexAddress) {

				entry.blas.Update(commandList, input);
				FrameProfiler::GetInstance().AddBLASRefit(
					static_cast<uint32_t>(geometries.size()));
				requireTlasRefit = true;
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

				blas.Build(device, commandList, lodInput);
				FrameProfiler::GetInstance().AddBLASBuild(
					static_cast<uint32_t>(lodGeometries.size()));
				requireTlasRebuild = true;
			} else if (geometryChanged) {

				blas.Update(commandList, lodInput);
				FrameProfiler::GetInstance().AddBLASRefit(
					static_cast<uint32_t>(lodGeometries.size()));
				requireTlasRefit = true;
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

				auto blasIt = blases_.find(key);
				if (blasIt != blases_.end() &&
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
					blases_[key];
				blas.Build(device, commandList, lodInput);
				FrameProfiler::GetInstance().
					AddBLASBuild(
						static_cast<uint32_t>(
							lodGeometries.size()));
				requireTlasRebuild = true;
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
		const uint32_t shaderInstanceIndex =
			static_cast<uint32_t>(sceneInstanceScratch_.size());
		sceneInstanceScratch_.emplace_back(instanceShaderData);
		scenePickRecordOffsets_.emplace_back(pickRecordOffset);

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
			static_cast<uint32_t>(tlasInstances.size());
		tlasInstances.emplace_back(instance);
		tlasEntityKeys.emplace_back(
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
			meshLODRecordIndices.emplace_back(
				static_cast<uint32_t>(
					meshLODInstances.size()));
			meshLODInstances.emplace_back(
				lodInstance);
		} else {
			meshLODRecordIndices.emplace_back(UINT32_MAX);
		}
	}

	for (const CollectedFillMeshInstance& src : sceneFillMeshes) {

		const FillMeshRendererComponent& renderer = *src.renderer;
		++blasGeometryCount;
		const FillMeshRuntimeStateComponent* state =
			src.world->TryGetComponent<FillMeshRuntimeStateComponent>(src.entity);
		const uint32_t geometryGeneration =
			state ? state->geometryGeneration : 0;

		FillMeshRTKey key{};
		key.world = src.world;
		key.entity = src.entity;

		FillMeshRaytracingResource& resource = fillMeshRTResources_[key];
		resource.lastUsedFrame =
			GraphicsFrameState::GetFrameSerial();
		if (resource.builtGeneration != geometryGeneration || !resource.blas.IsBuilt()) {

			if (!BuildFillMeshRaytracingResource(device, commandList, uploadService, src, resource)) {
				continue;
			}
			FrameProfiler::GetInstance().AddBLASBuild(1);
			// ジオメトリ変化でBLASを作り直したためTLASは完全再構築する
			requireTlasRebuild = true;
		} else {

			FrameProfiler::GetInstance().AddBLASSkip(1);
		}
		if (!resource.blas.GetResource() || !resource.vertexSRV.buffer || !resource.indexSRV.buffer) {
			continue;
		}

		const uint32_t subMeshDataIndex = static_cast<uint32_t>(sceneSubMeshScratch_.size());

		const MeshSubMeshShaderData subMeshData = MakeFlatSubMeshData(renderer.color);
		sceneSubMeshScratch_.emplace_back(subMeshData);

		RaytracingInstanceShaderData instanceShaderData{};
		instanceShaderData.vertexDescriptorIndex = resource.vertexSRV.srvIndex;
		instanceShaderData.indexDescriptorIndex = resource.indexSRV.srvIndex;
		instanceShaderData.vertexOffset = 0;
		instanceShaderData.geometryDataOffset =
			static_cast<uint32_t>(sceneGeometryScratch_.size());
		const uint32_t shaderInstanceIndex = static_cast<uint32_t>(sceneInstanceScratch_.size());
		sceneInstanceScratch_.emplace_back(instanceShaderData);

		const uint32_t pickRecordIndex =
			static_cast<uint32_t>(scenePickRecords_.size());
		MeshSubMeshPickRecord pickRecord{};
		pickRecord.entity = src.entity;
		pickRecord.subMeshIndex = 0;
		scenePickRecords_.emplace_back(pickRecord);
		scenePickRecordOffsets_.emplace_back(pickRecordIndex);

		RaytracingGeometryShaderData geometryData{};
		geometryData.subMeshDataIndex = subMeshDataIndex;
		geometryData.pickRecordIndex = pickRecordIndex;
		sceneGeometryScratch_.emplace_back(geometryData);

		RaytracingTLASInstance instance{};
		instance.blas = resource.blas.GetResource();
		instance.instanceID = shaderInstanceIndex;
		instance.hitGroupIndex = 0;
		instance.mask = kRaytracingMaskAlwaysHit |
			kRaytracingMaskReflectionCaster;
		if (src.castShadows) {
			instance.mask |= kRaytracingMaskShadowCaster;
		}
		instance.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance.worldMatrix = src.worldMatrix;
		tlasInstances.emplace_back(instance);
		tlasEntityKeys.emplace_back(
			SceneEntityKey{
				.world = src.world,
				.entity = src.entity,
			});
		meshLODRecordIndices.emplace_back(UINT32_MAX);
	}

	// Primitiveは形状ハッシュ単位で共有BLASを使い、インスタンスごとにTLASへ登録する
	for (const CollectedPrimitiveInstance& src : scenePrimitives) {

		const PrimitiveRendererComponent& renderer = *src.renderer;
		++blasGeometryCount;

		PrimitiveGeometry* geometry = primitiveGeometryManager->GetOrCreate(graphicsCore, src.geometryHash, renderer);
		if (!geometry) {
			continue;
		}
		// BLASを共有ジオメトリから作る、初めて作ったフレームだけTLASを完全再構築する
		const bool wasBuilt = geometry->blasBuilt;
		if (!primitiveGeometryManager->EnsureBLAS(device, commandList, *geometry)) {
			continue;
		}
		if (!wasBuilt) {
			FrameProfiler::GetInstance().AddBLASBuild(1);
			requireTlasRebuild = true;
		} else {

			FrameProfiler::GetInstance().AddBLASSkip(1);
		}

		const uint32_t subMeshDataIndex = static_cast<uint32_t>(sceneSubMeshScratch_.size());

		const MeshSubMeshShaderData subMeshData = MakeFlatSubMeshData(Color4::White());
		sceneSubMeshScratch_.emplace_back(subMeshData);

		RaytracingInstanceShaderData instanceShaderData{};
		instanceShaderData.vertexDescriptorIndex = geometry->vertexBuffer.srvIndex;
		instanceShaderData.indexDescriptorIndex = geometry->indexSRV.srvIndex;
		instanceShaderData.vertexOffset = 0;
		instanceShaderData.geometryDataOffset =
			static_cast<uint32_t>(sceneGeometryScratch_.size());
		const uint32_t shaderInstanceIndex = static_cast<uint32_t>(sceneInstanceScratch_.size());
		sceneInstanceScratch_.emplace_back(instanceShaderData);

		const uint32_t pickRecordIndex =
			static_cast<uint32_t>(scenePickRecords_.size());
		MeshSubMeshPickRecord pickRecord{};
		pickRecord.entity = src.entity;
		pickRecord.subMeshIndex = 0;
		scenePickRecords_.emplace_back(pickRecord);
		scenePickRecordOffsets_.emplace_back(pickRecordIndex);

		RaytracingGeometryShaderData geometryData{};
		geometryData.subMeshDataIndex = subMeshDataIndex;
		geometryData.pickRecordIndex = pickRecordIndex;
		sceneGeometryScratch_.emplace_back(geometryData);

		RaytracingTLASInstance instance{};
		instance.blas = geometry->blas.GetResource();
		instance.instanceID = shaderInstanceIndex;
		instance.hitGroupIndex = 0;
		// CastShadow/CastReflectionに応じて影レイと反射レイの当たり判定を分ける
		instance.mask = kRaytracingMaskAlwaysHit;
		if (src.castShadows) {
			instance.mask |= kRaytracingMaskShadowCaster;
		}
		if (HasMeshRenderFlag(renderer.renderFlags, MeshRenderFlags::CastReflection)) {
			instance.mask |= kRaytracingMaskReflectionCaster;
		}
		instance.flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
		instance.worldMatrix = src.worldMatrix;
		tlasInstances.emplace_back(instance);
		tlasEntityKeys.emplace_back(
			SceneEntityKey{
				.world = src.world,
				.entity = src.entity,
			});
		meshLODRecordIndices.emplace_back(UINT32_MAX);
	}

	// TLASインスタンスがない場合は処理しない
	if (tlasInstances.empty()) {
		return;
	}
	FrameProfiler::GetInstance().SetTLASInstanceCount(
		static_cast<uint32_t>(tlasInstances.size()));

	// バッファ転送
	sceneInstances_.Upload(sceneInstanceScratch_);
	sceneGeometries_.Upload(sceneGeometryScratch_);
	sceneSubMeshes_.Upload(sceneSubMeshScratch_);
	sceneUploadFrameSerials_[GraphicsFrameState::GetCurrentIndex()] =
		GraphicsFrameState::GetFrameSerial();

	// TLASの構築、BLASを新規/作り直しした場合はrefitでは反映できないため完全再構築する
	const uint64_t tlasInstanceHash =
		ComputeTLASInstanceHash(tlasInstances);
	uint32_t changedInstanceCount = 0;
	if (cachedTLASInstances_.size() == tlasInstances.size()) {
		for (size_t index = 0;
			index < tlasInstances.size(); ++index) {

			const RaytracingTLASInstance& previous =
				cachedTLASInstances_[index];
			const RaytracingTLASInstance& current =
				tlasInstances[index];
			if (previous.blas != current.blas ||
				previous.instanceID != current.instanceID ||
				previous.hitGroupIndex != current.hitGroupIndex ||
				previous.mask != current.mask ||
				previous.flags != current.flags ||
				previous.worldMatrix != current.worldMatrix) {
				++changedInstanceCount;
			}
		}
	}
	const bool instanceCountChanged =
		tlas_.IsBuilt() &&
		cachedTLASInstanceCount_ != tlasInstances.size();
	const bool rebuildForTraceQuality =
		RequiresTLASRebuildForTraceQuality(
			tlasInstances.size(), changedInstanceCount);
	if (firstTLASBuild_ || !tlas_.IsBuilt() ||
		requireTlasRebuild || instanceCountChanged ||
		rebuildForTraceQuality) {

		tlas_.Build(device, commandList, tlasInstances, true);
		firstTLASBuild_ = false;
		FrameProfiler::GetInstance().AddTLASBuild();
	} else if (requireTlasRefit ||
		tlasInstanceHash_ != tlasInstanceHash) {

		tlas_.Update(commandList, tlasInstances);
		FrameProfiler::GetInstance().AddTLASRefit();
	} else {

		FrameProfiler::GetInstance().AddTLASSkip();
	}
	tlasInstanceHash_ = tlasInstanceHash;

	// 構築済みにする
	builtThisFrame_ = true;
	builtSceneInstanceID_ = context.sceneInstance->instanceID;
	cachedStaticScene_ = staticScene;
	cachedSceneInstanceID_ = context.sceneInstance->instanceID;
	cachedRenderRevision_ =
		renderBatch.GetSourceRenderRevision();
	cachedTransformRevision_ =
		renderBatch.GetSourceTransformRevision();
	cachedMeshResourceRevision_ = meshResourceRevision;
	cachedLODViewHash_ = lodViewHash;
	cachedBLASGeometryCount_ = blasGeometryCount;
	cachedTLASInstanceCount_ = static_cast<uint32_t>(tlasInstances.size());
	cachedTLASInstances_.clear();
	cachedTLASInstanceIndices_.clear();
	cachedMeshLODInstances_.clear();
	cachedMeshLODRecordIndices_.clear();
	if (staticScene) {
		cachedTLASInstances_ = tlasInstances;
		cachedMeshLODInstances_ =
			std::move(meshLODInstances);
		cachedMeshLODRecordIndices_ =
			std::move(meshLODRecordIndices);
		cachedTLASInstanceIndices_.reserve(
			tlasEntityKeys.size());
		for (uint32_t index = 0;
			index < static_cast<uint32_t>(
				tlasEntityKeys.size()); ++index) {
			cachedTLASInstanceIndices_.emplace(
				tlasEntityKeys[index], index);
		}
	}

	// 構築したシーン情報をコンテキストに渡す
	PublishBuiltScene(context);
}

void Engine::RaytracingSceneBuilder::UploadCachedSceneBuffers() {

	const uint32_t frameIndex = GraphicsFrameState::GetCurrentIndex();
	const uint64_t frameSerial = GraphicsFrameState::GetFrameSerial();
	if (sceneUploadFrameSerials_[frameIndex] == frameSerial) {
		return;
	}

	sceneInstances_.Upload(sceneInstanceScratch_);
	sceneGeometries_.Upload(sceneGeometryScratch_);
	sceneSubMeshes_.Upload(sceneSubMeshScratch_);
	sceneUploadFrameSerials_[frameIndex] = frameSerial;
}

void Engine::RaytracingSceneBuilder::CollectSceneMeshInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedMeshInstance>& outInstances) {

	outInstances.clear();

	// シーンインスタンスIDを取得する
	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		// メッシュ描画アイテムで、かつシーンインスタンスIDが一致するものを対象とする
		if (item.backendID != RenderBackendID::Mesh) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		const MeshRenderPayload* payload = renderBatch.GetPayload<MeshRenderPayload>(item);
		if (!payload || !payload->mesh) {
			continue;
		}

		// 収集したメッシュインスタンスの情報を追加する
		CollectedMeshInstance instance{};
		instance.meshAssetID = payload->mesh;
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = nullptr;
		instance.castShadows = item.castShadows;
		if (item.world && item.world->IsAlive(item.entity)) {
			if (item.world->HasComponent<MeshRendererComponent>(item.entity)) {

				instance.renderer = &item.world->GetComponent<MeshRendererComponent>(item.entity);
			}
		}
		outInstances.emplace_back(instance);
	}
}

void Engine::RaytracingSceneBuilder::CollectSceneFillMeshInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedFillMeshInstance>& outInstances) {

	outInstances.clear();

	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		if (item.backendID != RenderBackendID::FillMesh) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		if (!item.world || !item.world->IsAlive(item.entity)) {
			continue;
		}
		if (!item.world->HasComponent<FillMeshRendererComponent>(item.entity)) {
			continue;
		}

		const FillMeshRendererComponent& renderer = item.world->GetComponent<FillMeshRendererComponent>(item.entity);
		if (GetFillMeshTriangleIndices(*item.world, item.entity).empty()) {
			continue;
		}

		CollectedFillMeshInstance instance{};
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = &renderer;
		instance.castShadows = item.castShadows;
		outInstances.emplace_back(instance);
	}
}

void Engine::RaytracingSceneBuilder::CollectScenePrimitiveInstances(const RenderSceneBatch& renderBatch,
	const SceneExecutionContext& context, std::vector<CollectedPrimitiveInstance>& outInstances) {

	outInstances.clear();

	const UUID sceneInstanceID = context.sceneInstance ? context.sceneInstance->instanceID : UUID{};
	for (const RenderItem& item : renderBatch.GetItems()) {

		if (item.backendID != RenderBackendID::Primitive) {
			continue;
		}
		if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
			continue;
		}
		if (!item.world || !item.world->IsAlive(item.entity)) {
			continue;
		}
		if (!item.world->HasComponent<PrimitiveRendererComponent>(item.entity)) {
			continue;
		}

		const PrimitiveRendererComponent& renderer = item.world->GetComponent<PrimitiveRendererComponent>(item.entity);

		// 2D描画はスクリーン空間のUIなので影/反射の対象にしない
		if (IsPrimitiveScreen2D(renderer)) {
			continue;
		}

		CollectedPrimitiveInstance instance{};
		instance.entity = item.entity;
		instance.world = item.world;
		instance.worldMatrix = item.worldMatrix;
		if (context.view) {
			instance.worldMatrix = RenderBillboard::ResolveWorldMatrix(item, *context.view);
		}
		instance.renderer = &renderer;
		instance.castShadows = item.castShadows;
		// batchKeyは上書き分離を含むためBLAS共有には形状ハッシュを使う
		instance.geometryHash = PrimitiveMeshGenerator::ComputeHash(renderer);
		outInstances.emplace_back(instance);
	}
}

bool Engine::RaytracingSceneBuilder::BuildFillMeshRaytracingResource(ID3D12Device8* device,
	ID3D12GraphicsCommandList6* commandList, BufferUploadService& uploadService,
	const CollectedFillMeshInstance& src, FillMeshRaytracingResource& resource) {

	if (!src.renderer || !srvDescriptor_) {
		return false;
	}

	const std::span<const FillMeshPosition> positions =
		GetFillMeshPositions(*src.world, src.entity);
	const std::span<const FillMeshTriangleIndex> triangleIndices =
		GetFillMeshTriangleIndices(*src.world, src.entity);
	if (!HasValidFillMeshIndices(triangleIndices, positions.size())) {
		return false;
	}

	resource.Release(srvDescriptor_);
	resource.indexBuffer = {};
	resource.blas = {};

	std::vector<MeshVertex> vertices{};
	vertices.reserve(positions.size());
	for (const FillMeshPosition& position : positions) {

		MeshVertex vertex{};
		vertex.normal = Vector3(0.0f, 1.0f, 0.0f);
		vertex.tangent = Vector3(1.0f, 0.0f, 0.0f);
		vertex.tangentSign = 1.0f;
		vertex.uv = Vector2::AnyInit(0.0f);
		vertex.position =
			Vector4(position.value.x, 0.0f, position.value.z, 1.0f);
		vertices.emplace_back(vertex);
	}
	std::vector<uint32_t> indices{};
	indices.reserve(triangleIndices.size());
	for (const FillMeshTriangleIndex& index : triangleIndices) {
		indices.emplace_back(index.value);
	}

	CreateImmutableSRV(device, uploadService, *srvDescriptor_,
		resource.vertexSRV, vertices, L"FillMeshRTVertices");
	CreateImmutableSRV(device, uploadService, *srvDescriptor_,
		resource.indexSRV, indices, L"FillMeshRTIndices");
	resource.indexBuffer.Create(device, uploadService,
		indices,
		DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_GENERIC_READ);

	if (!resource.vertexSRV.buffer || !resource.indexSRV.buffer || !resource.indexBuffer.IsCreatedResource()) {
		resource.Release(srvDescriptor_);
		resource.indexBuffer = {};
		return false;
	}

	uploadService.SubmitBatch();

	RaytracingBLASGeometryInput geometry{};
	geometry.vertexAddress =
		resource.vertexSRV.buffer->GetResource()->GetGPUVirtualAddress() +
		offsetof(MeshVertex, position);
	geometry.vertexStride = sizeof(MeshVertex);
	geometry.vertexCount = static_cast<uint32_t>(vertices.size());
	geometry.indexAddress =
		resource.indexBuffer.GetResource()->GetGPUVirtualAddress();
	geometry.indexFormat = resource.indexBuffer.GetFormat();
	geometry.indexCount = static_cast<uint32_t>(indices.size());

	RaytracingBLASInput input{};
	input.geometries = std::span(&geometry, 1);
	input.allowUpdate = false;

	resource.blas.Build(device, commandList, input);
	const FillMeshRuntimeStateComponent* state =
		src.world->TryGetComponent<FillMeshRuntimeStateComponent>(src.entity);
	resource.builtGeneration = state ? state->geometryGeneration : 0;
	return resource.blas.IsBuilt();
}

uint64_t Engine::RaytracingSceneBuilder::ComputeTLASInstanceHash(
	std::span<const RaytracingTLASInstance> instances) {

	uint64_t hash = 1469598103934665603ull;
	Algorithm::HashCombine(hash,
		static_cast<uint64_t>(instances.size()));
	for (const RaytracingTLASInstance& instance : instances) {

		Algorithm::HashCombine(hash, instance.blas ?
			instance.blas->GetGPUVirtualAddress() : 0);
		Algorithm::HashCombine(hash, instance.instanceID);
		Algorithm::HashCombine(hash, instance.hitGroupIndex);
		Algorithm::HashCombine(hash, instance.mask);
		Algorithm::HashCombine(hash,
			static_cast<uint32_t>(instance.flags));
		for (uint32_t row = 0; row < 4; ++row) {
			for (uint32_t column = 0; column < 4; ++column) {

				Algorithm::HashCombine(hash, std::bit_cast<uint32_t>(
					instance.worldMatrix.m[row][column]));
			}
		}
	}
	return hash;
}

uint32_t Engine::RaytracingSceneBuilder::ResolveTextureDescriptorIndex(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, AssetID textureAssetID) const {

	// すでに取得済みならそれを返す
	if (auto it = textureDescriptorIndexCache_.find(textureAssetID);
		it != textureDescriptorIndexCache_.end()) {
		return it->second;
	}

	const GPUTextureResource* errorTexture = graphicsCore.GetBuiltinTextureLibrary().GetErrorTexture();
	const uint32_t errorIndex = (errorTexture && errorTexture->valid) ? errorTexture->srvIndex : 0;

	const GPUTextureResource* texture = RuntimeTextureResolver::Resolve(graphicsCore, &assetDatabase, textureAssetID);
	if (texture && texture->valid && texture->srvIndex != UINT32_MAX) {

		// エラーテクスチャ以外が解決できている場合はキャッシュする
		if (texture != errorTexture) {
			textureDescriptorIndexCache_[textureAssetID] = texture->srvIndex;
		}
		return texture->srvIndex;
	}

	return errorIndex;
}

void Engine::RaytracingSceneBuilder::PublishBuiltScene(SceneExecutionContext& context) const {

	// RaytracingSceneInstances
	if (sceneInstances_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingSceneInstances",
			.resource = sceneInstances_.GetResource(),
			.gpuAddress = sceneInstances_.GetGPUAddress(),
			.srvGPUHandle = sceneInstances_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneInstanceScratch_.size()),
			.stride = sizeof(RaytracingInstanceShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingSceneInstances",
			.resource = sceneInstances_.GetResource(),
			.gpuAddress = sceneInstances_.GetGPUAddress(),
			.srvGPUHandle = sceneInstances_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneInstanceScratch_.size()),
			.stride = sizeof(RaytracingInstanceShaderData),
		});
	}

	// RaytracingGeometries
	if (sceneGeometries_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingGeometries",
			.resource = sceneGeometries_.GetResource(),
			.gpuAddress = sceneGeometries_.GetGPUAddress(),
			.srvGPUHandle = sceneGeometries_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneGeometryScratch_.size()),
			.stride = sizeof(RaytracingGeometryShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingGeometries",
			.resource = sceneGeometries_.GetResource(),
			.gpuAddress = sceneGeometries_.GetGPUAddress(),
			.srvGPUHandle = sceneGeometries_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneGeometryScratch_.size()),
			.stride = sizeof(RaytracingGeometryShaderData),
			});
	}

	// RaytracingSubMeshes
	if (sceneSubMeshes_.GetResource()) {
		context.bufferRegistry.Register({
			.alias = "RaytracingSubMeshes",
			.resource = sceneSubMeshes_.GetResource(),
			.gpuAddress = sceneSubMeshes_.GetGPUAddress(),
			.srvGPUHandle = sceneSubMeshes_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneSubMeshScratch_.size()),
			.stride = sizeof(MeshSubMeshShaderData),
			});
		context.bufferRegistry.Register({
			.alias = "gRaytracingSubMeshes",
			.resource = sceneSubMeshes_.GetResource(),
			.gpuAddress = sceneSubMeshes_.GetGPUAddress(),
			.srvGPUHandle = sceneSubMeshes_.GetGPUHandle(),
			.elementCount = static_cast<uint32_t>(sceneSubMeshScratch_.size()),
			.stride = sizeof(MeshSubMeshShaderData),
			});
	}
	context.raytracing.tlasResource = tlas_.GetResource();
	context.raytracing.instanceCount = static_cast<uint32_t>(sceneInstanceScratch_.size());
}
