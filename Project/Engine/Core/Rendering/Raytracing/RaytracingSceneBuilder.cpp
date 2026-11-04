#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include "RaytracingSceneGeometryUtility.h"
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Rendering/Materials/MaterialRenderPolicy.h>
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
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <memory>
#include <span>
#include <unordered_set>
#include <variant>

using namespace Engine::RaytracingSceneGeometryUtility;

//============================================================================
//	RaytracingSceneBuilder classMethods
//============================================================================
void Engine::RaytracingSceneBuilder::Init(GraphicsCore& graphicsCore) {

	if (initialized_) return;
	scene_.result.Init(graphicsCore);
	scene_.tlasState.ResetState();
	initialized_ = true;
}

void Engine::RaytracingSceneBuilder::Finalize() {

	if (!initialized_) return;
	SceneState& state = scene_;
	state.result.Release();
	state.materialResolver.Clear();
	state.cachedTLASInstances.clear();
	state.cachedTLASInstanceIndices.clear();
	state.cachedMeshInstances.clear();
	state.publishedBLASKeys.clear();
	state.buildingMeshOwnerships.clear();
	state.publishedMeshOwnerships.clear();
	state.renderTextureInputs.clear();
	state.materialRevision.reset();
	state.sceneMaterialGeneration = 0;
	state.cachedSceneMaterialHash = 0;
	state.textureRevision = 0;
	state.tlasState.ResetState();
	state.builtThisFrame = false;
	state.builtWorld = nullptr;
	state.builtWorldLifetime.reset();
	state.builtView = nullptr;
	state.builtSceneInstanceID = {};
	state.cachedStaticScene = false;
	state.cachedWorld = nullptr;
	state.cachedWorldLifetime.reset();
	state.cachedExtractorRevision.reset();
	state.cachedSceneInstanceID = {};
	state.cachedRenderRevision = 0;
	state.cachedTransformRevision = 0;
	state.cachedMeshResourceRevision = 0;
	state.cachedGeometryContentHash = 0;
	state.cachedBLASGeometryCount = 0;
	state.cachedTLASInstanceCount = 0;
	blasCache_.Clear();
	initialized_ = false;
}

void Engine::RaytracingSceneBuilder::BeginFrame(GraphicsCore& graphicsCore) {

	if (!initialized_) Init(graphicsCore);
	scene_.builtThisFrame = false;
	scene_.builtWorld = nullptr;
	scene_.builtWorldLifetime.reset();
	scene_.builtSceneInstanceID = {};
	blasCache_.CollectExpired();
	if (blasCache_.ProcessCompactions(graphicsCore)) {
		// 圧縮後のGPUアドレスでTLASを作り直す
		scene_.cachedStaticScene = false;
	}
}

void Engine::RaytracingSceneBuilder::BuildForScene(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase,
	RenderAssetLibrary& assetLibrary, MaterialResolver& materialResolver, MeshRenderBackend* meshBackend,
	PrimitiveGeometryManager* primitiveGeometryManager, const RenderSceneBatch& renderBatch, SceneExecutionContext& context) {

	const auto& featureController = graphicsCore.GetDXObject().GetFeatureController();
	const bool canBuildRaytracingScene =
		featureController.GetSupport().SupportsRayTracingPath() &&
		(graphicsCore.GetDXObject().ShouldBuildRaytracingScene() ||
		(context.view && context.view->FindCamera(RenderCameraDomain::Perspective) && !context.disableInlineRayTracing &&
		 context.postProcessCamera.surfaceSettings.ambientOcclusionEnabled &&
		 featureController.GetSupport().SupportsInlineRayTracingPath() && featureController.GetSupport().SupportsShaderModel6_6()));
	if (!canBuildRaytracingScene) {
		return;
	}
	if (!context.sceneInstance) {
		return;
	}

	SceneState& state = scene_;

	// Texture差し替え後は静的Sceneも新しい番号で再構築する
	uint64_t textureRevision = graphicsCore.GetTextureUploadService().GetContentRevision() +
		RuntimeTextureResolver::GetBindingRevision();
	Algorithm::HashCombine(textureRevision, MaterialRenderPolicy::GetDefaultMaterialHash());
	if (state.textureRevision != textureRevision) {
		state.textureRevision = textureRevision;
		state.materialResolver.Clear();
		state.cachedStaticScene = false;
		state.builtThisFrame = false;
	}

	// 静的SceneでもCamera出力を読み取り状態へ戻す
	for (const AssetID texture : state.renderTextureInputs) {
		RuntimeTextureResolver::ResolveBindless(graphicsCore, &assetDatabase, texture);
	}

	const uint64_t meshResourceRevision = meshBackend ? meshBackend->GetMeshResourceRevision() : 0;
	// Worldに変更がなくてもMaterialの編集結果を構築し直す
	if (state.materialRevision != assetLibrary.GetMaterialRevision()) {
		state.materialRevision = assetLibrary.GetMaterialRevision();
		state.cachedStaticScene = false;
		state.builtThisFrame = false;
	}
	// 同じ描画条件だけ構築済み結果を共有する
	if (state.builtThisFrame && state.builtWorld == context.world && state.builtWorldLifetime && state.builtWorldLifetime->IsAlive() &&
		state.builtSceneInstanceID == context.sceneInstance->instanceID && state.builtRenderRevision == renderBatch.GetSourceRevision() &&
		state.builtMeshResourceRevision == meshResourceRevision && (state.builtView == context.view || state.cachedStaticScene)) {
		PublishBuiltScene(context, state);
		return;
	}
	auto recordBuiltScene = [&]() {
		state.builtThisFrame = true;
		state.builtWorld = context.world;
		state.builtWorldLifetime = context.world ? context.world->GetLifetime() : nullptr;
		state.builtRenderRevision = renderBatch.GetSourceRevision();
		state.builtMeshResourceRevision = meshResourceRevision;
		state.builtView = context.view;
		state.builtSceneInstanceID = context.sceneInstance->instanceID;
	};
	bool matchesStaticScene = state.cachedStaticScene && !state.materialResolver.HasPendingTextures() &&
		state.cachedWorld == context.world && state.cachedWorldLifetime && state.cachedWorldLifetime->IsAlive() &&
		renderBatch.MatchesExtractors(state.cachedExtractorRevision) &&
		state.cachedSceneInstanceID == context.sceneInstance->instanceID &&
		state.cachedRenderRevision == renderBatch.GetSourceRenderRevision() &&
		state.cachedMeshResourceRevision == meshResourceRevision && state.tlasState.IsBuilt();
	if (matchesStaticScene) {
		for (const auto& key : state.publishedBLASKeys) {
			const auto entry = blasCache_.blases_.find(key);
			if (entry == blasCache_.blases_.end() || !entry->second.IsBuilt()) {
				matchesStaticScene = false;
				break;
			}
		}
	}
	if (matchesStaticScene && primitiveGeometryManager) {

		// 画面外のPrimitiveもTLAS参照中は共有形状を保持
		for (auto it = state.cachedTLASInstanceIndices.begin(); it != state.cachedTLASInstanceIndices.end();) {

			const auto key = it->first;
			it = state.cachedTLASInstanceIndices.equal_range(key).second;
			const auto* renderer = key.world->TryGetComponent<PrimitiveRendererComponent>(key.entity);
			if (!renderer) continue;
			const auto* geometry = primitiveGeometryManager->GetOrCreate(graphicsCore, *renderer);
			if (!geometry || !geometry->blasBuilt) {
				matchesStaticScene = false;
				break;
			}
		}
	}
	if (matchesStaticScene) {

		const uint64_t currentFrame = GraphicsFrameState::GetFrameSerial();
		for (const CachedMeshInstance& record : state.cachedMeshInstances) {

			if (!record.tracksInstanceLayout) {
				continue;
			}
			StaticInstanceBLASKey key{};
			key.world = record.world;
			key.worldLifetime = record.world->GetLifetime();
			key.entity = record.entity;
			key.meshAssetID = record.meshAssetID;
			key.reloadGeneration = record.reloadGeneration;
			auto entry = blasCache_.staticInstanceBLASes_.find(key);
			if (entry == blasCache_.staticInstanceBLASes_.end() || !entry->second.blas.IsBuilt()) {
				matchesStaticScene = false;
				break;
			}
			entry->second.lastUsedFrame = currentFrame;
		}
	}

	// 静的Sceneの構築結果とTransform差分を再利用する
	auto tryReuseStaticScene = [&]() -> bool {
		if (matchesStaticScene && state.cachedTransformRevision == renderBatch.GetSourceTransformRevision()) {

			state.tlasState.MaintainTraceQuality(graphicsCore, state.cachedTLASInstances);
			state.result.UploadCached();
			FrameProfiler::GetInstance().AddBLASSkip(state.cachedBLASGeometryCount);
			FrameProfiler::GetInstance().SetTLASInstanceCount(state.cachedTLASInstanceCount);
			recordBuiltScene();
			PublishBuiltScene(context, state);
			return true;
		}
		// 描画休止中の配置を現在Frameの差分だけで補完しない
		const uint64_t lastUploadFrame = *std::max_element(state.result.sceneUploadFrameSerials_.begin(),
			state.result.sceneUploadFrameSerials_.end());
		if (matchesStaticScene && lastUploadFrame + 1u >= GraphicsFrameState::GetFrameSerial() &&
			renderBatch.HasCompleteTransformChanges() && !state.cachedTLASInstances.empty()) {

			bool transformChanged = false;
			uint32_t changedInstanceCount = 0;
			for (const RenderTransformChange& change : renderBatch.GetTransformChanges()) {
				SceneEntityKey key{};
				key.world = change.world;
				key.entity = change.entity;
				const auto [begin, end] = state.cachedTLASInstanceIndices.equal_range(key);
				for (auto it = begin; it != end; ++it) {
					RaytracingTLASInstance& instance = state.cachedTLASInstances[it->second];
					if (instance.worldMatrix == change.worldMatrix) {
						continue;
					}
					instance.worldMatrix = change.worldMatrix;
					transformChanged = true;
					++changedInstanceCount;
				}
			}

			if (transformChanged) {
				const bool rebuildForTraceQuality =
					RequiresTLASRebuildForTraceQuality(state.cachedTLASInstances.size(), changedInstanceCount);
				state.tlasState.RefitORRebuild(graphicsCore, state.cachedTLASInstances, rebuildForTraceQuality);
			} else {
				state.tlasState.MaintainTraceQuality(graphicsCore, state.cachedTLASInstances);
			}

			state.tlasState.RecordInstances(state.cachedTLASInstances);
			state.cachedTransformRevision = renderBatch.GetSourceTransformRevision();
			state.result.UploadCached();
			FrameProfiler::GetInstance().AddBLASSkip(state.cachedBLASGeometryCount);
			FrameProfiler::GetInstance().SetTLASInstanceCount(state.cachedTLASInstanceCount);
			recordBuiltScene();
			PublishBuiltScene(context, state);
			return true;
		}
		return false;
	};
	if (tryReuseStaticScene()) {
		return;
	}
	state.cachedStaticScene = false;

	state.result.scenePickRecords_.clear();
	state.result.scenePickRecordOffsets_.clear();

	// シーン内の可視メッシュインスタンスを収集する
	std::vector<CollectedMeshInstance> sceneMeshes;
	if (meshBackend) {
		CollectSceneMeshInstances(renderBatch, *context.sceneInstance, context.view, sceneMeshes);
	}
	std::vector<CollectedPrimitiveInstance> scenePrimitives;
	if (primitiveGeometryManager) {
		CollectScenePrimitiveInstances(renderBatch, *context.sceneInstance, context.view, scenePrimitives);
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
	state.renderTextureInputs.clear();
	state.result.sceneInstanceScratch_.clear();
	state.result.sceneGeometryScratch_.clear();
	state.result.sceneGeometryBounds_.clear();
	state.result.sceneSubMeshScratch_.clear();
	state.materialResolver.ResetPending();

	// BLASの構築とTLASインスタンスの準備
	std::vector<RaytracingTLASInstance> tlasInstances;
	tlasInstances.reserve(sceneMeshes.size() + scenePrimitives.size());
	std::vector<SceneEntityKey> tlasEntityKeys;
	tlasEntityKeys.reserve(sceneMeshes.size() + scenePrimitives.size());
	std::vector<CachedMeshInstance> meshInstances;
	meshInstances.reserve(sceneMeshes.size());

	// BLASの差し替えをTLASの再構築へ反映
	bool requireTLASRebuild = false;
	bool blasContentsChanged = false;
	uint64_t geometryContentHash = 1469598103934665603ull;
	// Billboardの行列はViewごとに作り直す
	bool staticScene = std::none_of(sceneMeshes.begin(), sceneMeshes.end(), [](const auto& item) {
		return item.viewDependent;
	}) && std::none_of(scenePrimitives.begin(), scenePrimitives.end(), [](const auto& item) { return item.viewDependent; });
	uint32_t blasGeometryCount = 0;

	SceneBuildWork work{
		.graphicsCore = graphicsCore,
		.scene = state,
		.runtimeFeatures = featureController.GetRuntimeFeatures(),
		.assetDatabase = assetDatabase,
		.assetLibrary = assetLibrary,
		.materialResolver = materialResolver,
		.meshBackend = meshBackend,
		.primitiveGeometryManager = primitiveGeometryManager,
		.device = device,
		.commandList = commandList,
		.tlasInstances = tlasInstances,
		.tlasEntityKeys = tlasEntityKeys,
		.meshInstances = meshInstances,
		.requireTLASRebuild = requireTLASRebuild,
		.blasContentsChanged = blasContentsChanged,
		.geometryContentHash = geometryContentHash,
		.staticScene = staticScene,
		.blasGeometryCount = blasGeometryCount,
	};
	state.publishedBLASKeys.clear();
	state.buildingMeshOwnerships.clear();
	BuildMeshInstances(sceneMeshes, work);

	// 形状ごとにBLASを共有してPrimitiveを追加
	BuildPrimitiveInstances(scenePrimitives, work);

	// TLASインスタンスがない場合は処理しない
	if (tlasInstances.empty()) {

		// 描画対象がなくなったSceneの所有を解除する
		state.tlasState.ResetState();
		state.buildingMeshOwnerships.clear();
		state.publishedMeshOwnerships.clear();
		state.publishedBLASKeys.clear();
		state.materialResolver.CommitTextures();
		return;
	}
	FrameProfiler::GetInstance().SetTLASInstanceCount(static_cast<uint32_t>(tlasInstances.size()));

	// バッファ転送
	state.result.Upload();

	// 共有BLASの内容の変化をTLASへ反映する
	blasContentsChanged |= state.cachedGeometryContentHash != geometryContentHash;
	state.cachedGeometryContentHash = geometryContentHash;

	// BLASの変更に合わせてTLASを構築
	state.tlasState.BuildORUpdate(
		graphicsCore, tlasInstances, state.cachedTLASInstances, state.cachedTLASInstanceCount, requireTLASRebuild, blasContentsChanged);

	// 構築済みにする
	const uint64_t sceneMaterialHash = ComputeSceneMaterialHash(state.result.sceneSubMeshScratch_);
	if (state.cachedSceneMaterialHash != sceneMaterialHash || state.sceneMaterialGeneration == 0) {

		state.cachedSceneMaterialHash = sceneMaterialHash;
		++state.sceneMaterialGeneration;
		if (state.sceneMaterialGeneration == 0) {
			state.sceneMaterialGeneration = 1;
		}
	}
	recordBuiltScene();
	state.cachedStaticScene = staticScene;
	state.cachedWorld = context.world;
	state.cachedWorldLifetime = context.world ? context.world->GetLifetime() : nullptr;
	state.cachedExtractorRevision = renderBatch.GetExtractorRevision();
	state.cachedSceneInstanceID = context.sceneInstance->instanceID;
	state.cachedRenderRevision = renderBatch.GetSourceRenderRevision();
	state.cachedTransformRevision = renderBatch.GetSourceTransformRevision();
	state.cachedMeshResourceRevision = meshResourceRevision;
	state.cachedBLASGeometryCount = blasGeometryCount;
	state.cachedTLASInstanceCount = static_cast<uint32_t>(tlasInstances.size());
	state.cachedTLASInstances = tlasInstances;
	state.cachedTLASInstanceIndices.clear();
	state.cachedMeshInstances.clear();
	if (staticScene) {
		state.cachedMeshInstances = std::move(meshInstances);
		state.cachedTLASInstanceIndices.reserve(tlasEntityKeys.size());
		for (uint32_t index = 0; index < static_cast<uint32_t>(tlasEntityKeys.size()); ++index) {
			state.cachedTLASInstanceIndices.emplace(tlasEntityKeys[index], index);
		}
	}

	// 構築したシーン情報をコンテキストに渡す
	PublishBuiltScene(context, state);
	state.materialResolver.CommitTextures();
	// 新しいTLASの公開まで旧Meshの所有を保持する
	state.publishedMeshOwnerships = std::move(state.buildingMeshOwnerships);
}

void Engine::RaytracingSceneBuilder::PublishBuiltScene(SceneExecutionContext& context, SceneState& state) {

	// TLASの再利用中も共有BLASを保持する
	for (const auto& key : state.publishedBLASKeys) {
		blasCache_.sharedBLASFrames_[key] = GraphicsFrameState::GetFrameSerial();
	}

	state.result.Publish(context, state.tlasState.GetResource(), state.sceneMaterialGeneration, !state.materialResolver.HasPendingTextures(), state.cachedTLASInstances);
	context.raytracing.geometryGeneration = state.tlasState.GetContentGeneration();
}
