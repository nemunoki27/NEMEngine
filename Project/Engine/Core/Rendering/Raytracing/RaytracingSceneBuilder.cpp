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

using namespace Engine::RaytracingSceneGeometryUtility;

//============================================================================
//	RaytracingSceneBuilder internal
//============================================================================

//============================================================================
//	RaytracingSceneBuilder classMethods
//============================================================================
void Engine::RaytracingSceneBuilder::Init(GraphicsCore& graphicsCore) {

	if (initialized_) {
		return;
	}

	// バッファ初期化
	result_.Init(graphicsCore);

	tlasState_.ResetState();
	initialized_ = true;
}

void Engine::RaytracingSceneBuilder::Finalize() {

	if (!initialized_) {
		return;
	}

	result_.Release();
	cachedTLASInstances_.clear();
	cachedTLASInstanceIndices_.clear();
	cachedMeshLODInstances_.clear();
	cachedMeshLODRecordIndices_.clear();

	materialResolver_.Clear();
	sceneMaterialGeneration_ = 0;
	cachedSceneMaterialHash_ = 0;

	blasCache_.Clear();
	tlasState_.ResetState();
	initialized_ = false;
	builtThisFrame_ = false;
	builtWorld_ = nullptr;
	builtSceneInstanceID_ = {};
	cachedStaticScene_ = false;
	cachedWorld_ = nullptr;
	cachedSceneInstanceID_ = {};
	cachedRenderRevision_ = 0;
	cachedTransformRevision_ = 0;
	cachedMeshResourceRevision_ = 0;
	cachedLODViewHash_ = 0;
	cachedBLASGeometryCount_ = 0;
	cachedTLASInstanceCount_ = 0;
}

void Engine::RaytracingSceneBuilder::BeginFrame(GraphicsCore& graphicsCore) {

	if (!initialized_) {
		Init(graphicsCore);
	}

	// フラグリセット
	builtThisFrame_ = false;
	builtWorld_ = nullptr;
	builtSceneInstanceID_ = {};

	blasCache_.CollectExpired();
}

void Engine::RaytracingSceneBuilder::BuildForScene(GraphicsCore& graphicsCore,
	AssetDatabase& assetDatabase, RenderAssetLibrary& assetLibrary,
	MaterialResolver& materialResolver, MeshRenderBackend* meshBackend,
	PrimitiveGeometryManager* primitiveGeometryManager,
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
	if (builtThisFrame_ && builtWorld_ == context.world &&
		builtSceneInstanceID_ == context.sceneInstance->instanceID) {
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
				auto blasIt = blasCache_.staticInstanceBLASes_.find(key);
				if (blasIt == blasCache_.staticInstanceBLASes_.end() ||
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
				auto blasIt = blasCache_.blases_.find(key);
				if (blasIt == blasCache_.blases_.end() ||
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
				if (result_.sceneGeometryScratch_.size() <=
					dataIndex) {
					break;
				}
				result_.sceneGeometryScratch_[dataIndex].
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
		!materialResolver_.HasPendingTextures() &&
		cachedWorld_ == context.world &&
		cachedSceneInstanceID_ == context.sceneInstance->instanceID &&
		cachedRenderRevision_ ==
			renderBatch.GetSourceRenderRevision() &&
		cachedMeshResourceRevision_ == meshResourceRevision &&
		tlasState_.IsBuilt();
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
			auto entry = blasCache_.staticInstanceBLASes_.find(key);
			if (entry != blasCache_.staticInstanceBLASes_.end()) {
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

			tlasState_.RefitORRebuild(
				graphicsCore, cachedTLASInstances_, false);
			tlasState_.RecordInstances(cachedTLASInstances_);
		} else {
			FrameProfiler::GetInstance().AddTLASSkip();
		}
		cachedLODViewHash_ = lodViewHash;
		result_.UploadCached();
		FrameProfiler::GetInstance().AddBLASSkip(cachedBLASGeometryCount_);
		FrameProfiler::GetInstance().SetTLASInstanceCount(cachedTLASInstanceCount_);
		builtThisFrame_ = true;
		builtWorld_ = context.world;
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
			const bool rebuildForTraceQuality =
				RequiresTLASRebuildForTraceQuality(
					cachedTLASInstances_.size(),
					changedInstanceCount);
			tlasState_.RefitORRebuild(graphicsCore,
				cachedTLASInstances_, rebuildForTraceQuality);
		} else {
			FrameProfiler::GetInstance().AddTLASSkip();
		}

		tlasState_.RecordInstances(cachedTLASInstances_);
		cachedTransformRevision_ =
			renderBatch.GetSourceTransformRevision();
		cachedLODViewHash_ = lodViewHash;
		result_.UploadCached();
		FrameProfiler::GetInstance().AddBLASSkip(
			cachedBLASGeometryCount_);
		FrameProfiler::GetInstance().SetTLASInstanceCount(
			cachedTLASInstanceCount_);
		builtThisFrame_ = true;
		builtWorld_ = context.world;
		builtSceneInstanceID_ =
			context.sceneInstance->instanceID;
		PublishBuiltScene(context);
		return;
	}
	cachedStaticScene_ = false;

	result_.scenePickRecords_.clear();
	result_.scenePickRecordOffsets_.clear();

	// シーン内の可視メッシュインスタンスを収集する
	std::vector<CollectedMeshInstance> sceneMeshes;
	if (meshBackend) {
		CollectSceneMeshInstances(renderBatch, context, sceneMeshes);
	}
	std::vector<CollectedPrimitiveInstance> scenePrimitives;
	if (primitiveGeometryManager) {
		CollectScenePrimitiveInstances(renderBatch, context, scenePrimitives);
	}
	if (sceneMeshes.empty() && scenePrimitives.empty()) {
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

	// データクリア
	result_.sceneInstanceScratch_.clear();
	result_.sceneGeometryScratch_.clear();
	result_.sceneSubMeshScratch_.clear();
	materialResolver_.ResetPending();

	// BLASの構築とTLASインスタンスの準備
	std::vector<RaytracingTLASInstance> tlasInstances;
	tlasInstances.reserve(sceneMeshes.size() + scenePrimitives.size());
	std::vector<SceneEntityKey> tlasEntityKeys;
	tlasEntityKeys.reserve(sceneMeshes.size() + scenePrimitives.size());
	std::vector<CachedMeshLODInstance> meshLODInstances;
	meshLODInstances.reserve(sceneMeshes.size());
	std::vector<uint32_t> meshLODRecordIndices;
	meshLODRecordIndices.reserve(sceneMeshes.size() + scenePrimitives.size());

	// BLASリソースを新規/作り直しした場合はTLASのrefitでは反映できないため完全再構築する
	bool requireTlasRebuild = false;
	bool staticScene = true;
	uint32_t blasGeometryCount = 0;

	SceneBuildWork work{
		.graphicsCore = graphicsCore,
		.assetDatabase = assetDatabase,
		.assetLibrary = assetLibrary,
		.materialResolver = materialResolver,
		.meshBackend = meshBackend,
		.primitiveGeometryManager = primitiveGeometryManager,
		.runtimeFeatures = runtimeFeatures,
		.lodView = lodView,
		.device = device,
		.commandList = commandList,
		.tlasInstances = tlasInstances,
		.tlasEntityKeys = tlasEntityKeys,
		.meshLODInstances = meshLODInstances,
		.meshLODRecordIndices = meshLODRecordIndices,
		.requireTlasRebuild = requireTlasRebuild,
		.staticScene = staticScene,
		.blasGeometryCount = blasGeometryCount,
	};
	BuildMeshInstances(sceneMeshes, work);

	// Primitiveは形状ハッシュ単位で共有BLASを使い、インスタンスごとにTLASへ登録する
	BuildPrimitiveInstances(scenePrimitives, work);

	// TLASインスタンスがない場合は処理しない
	if (tlasInstances.empty()) {
		return;
	}
	FrameProfiler::GetInstance().SetTLASInstanceCount(
		static_cast<uint32_t>(tlasInstances.size()));

	// バッファ転送
	result_.Upload();

	// TLASの構築、BLASを新規/作り直しした場合はrefitでは反映できないため完全再構築する
	tlasState_.BuildORUpdate(graphicsCore, tlasInstances, cachedTLASInstances_,
		cachedTLASInstanceCount_, requireTlasRebuild);

	// 構築済みにする
	const uint64_t sceneMaterialHash =
		ComputeSceneMaterialHash(result_.sceneSubMeshScratch_);
	if (cachedSceneMaterialHash_ != sceneMaterialHash ||
		sceneMaterialGeneration_ == 0) {

		cachedSceneMaterialHash_ = sceneMaterialHash;
		++sceneMaterialGeneration_;
		if (sceneMaterialGeneration_ == 0) {
			sceneMaterialGeneration_ = 1;
		}
	}
	builtThisFrame_ = true;
	builtWorld_ = context.world;
	builtSceneInstanceID_ = context.sceneInstance->instanceID;
	cachedStaticScene_ = staticScene;
	cachedWorld_ = context.world;
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

void Engine::RaytracingSceneBuilder::PublishBuiltScene(SceneExecutionContext& context) const {

	result_.Publish(context, tlasState_.GetResource(), sceneMaterialGeneration_, !materialResolver_.HasPendingTextures());
}

//============================================================================
//	RaytracingSceneBuilder classMethods
//============================================================================

namespace Engine {

	size_t RaytracingSceneBuilder::SceneEntityKeyHash::operator()(
		const SceneEntityKey& key) const noexcept {

		size_t h = std::hash<void*>{}(key.world);
		h ^= (std::hash<uint32_t>{}(
			key.entity.index) << 1);
		h ^= (std::hash<uint32_t>{}(
			key.entity.generation) << 2);
		return h;
	}

}
