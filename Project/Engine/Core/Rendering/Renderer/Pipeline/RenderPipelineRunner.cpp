#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include "RuntimeRenderPreloader.h"
#include "RenderPipelineUtility.h"
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Renderer/Views/GameViewCameraSnapshot.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/ViewLightCollector.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphArtifactCache.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphBindingNames.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileService.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <unordered_set>

using namespace Engine;

const RenderTexture2D* RenderPipelineRunner::FindViewColorTexture(RenderViewKind kind, const std::string& name) const {

	const auto& state = FindCameraState(kind);
	return state.targetRegistry.FindColorByName(name);
}

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================

void RenderPipelineRunner::PreloadRuntimeAssets(
	GraphicsCore& graphicsCore, AssetDatabase& assetDatabase, std::span<const AssetID> assets) {

	RuntimeRenderPreloadContext context{
		.assetLibrary = renderAssetLibrary_,
		.assetGenerator = postProcessAssetGenerator_,
		.backends = backendRegistry_,
		.meshBackend = meshBackend_,
		.particleBackend = particleBackend_,
		.viewport = *viewportRenderService_,
		.gameResources = gameViewState_.resources,
		.pipelines = pipelineStateCache_,
		.raytracingPipelines = raytracingPipelineStateCache_,
	};
	RuntimeRenderPreloader::Preload(graphicsCore, assetDatabase, context, assets);
}

void RenderPipelineRunner::ReloadMesh(AssetID meshAssetID) {

	// 本番用とプレビュー用の両メッシュバックエンドへ再ロードを伝える
	if (meshBackend_) {
		meshBackend_->RequestMeshReload(meshAssetID);
	}
	if (particleBackend_) {
		particleBackend_->RequestMeshReload(meshAssetID);
	}
	if (previewResources_.previewMeshBackend_) {
		previewResources_.previewMeshBackend_->RequestMeshReload(meshAssetID);
	}
}

void RenderPipelineRunner::ReloadMaterial(AssetID materialAssetID) {

	assetReloadService_.ReloadMaterial(materialAssetID);
}

void RenderPipelineRunner::ReloadShader(AssetID shaderAssetID) {

	assetReloadService_.ReloadShader(shaderAssetID);
}

void RenderPipelineRunner::ReloadPipeline(AssetID pipelineAssetID) {

	assetReloadService_.ReloadPipeline(pipelineAssetID);
}

bool RenderPipelineRunner::ReloadMaterialDependencies(AssetDatabase& assetDatabase, AssetID materialAssetID) {

	return assetReloadService_.ReloadMaterialDependencies(assetDatabase, materialAssetID);
}

void RenderPipelineRunner::ReloadAsset(AssetDatabase& assetDatabase, AssetID assetID) {

	assetReloadService_.ReloadAsset(assetDatabase, assetID);
}

Engine::RenderTexture2D* RenderPipelineRunner::GetViewGBufferTexture(RenderViewKind kind, GBufferAttachment attachment) {

	// 表示対象CameraのGBufferを参照する
	RenderPathResources& resources = FindCameraState(kind).resources;
	return resources.GetGBuffer(attachment);
}

const Engine::ShaderReflectionInfo* RenderPipelineRunner::FindMaterialDrawReflection(const MaterialAsset& material) const {

	const MaterialPassBinding* drawPass = FindPass(material, MaterialPassKind::Draw);
	if (drawPass && drawPass->pipeline) {

		const ShaderReflectionInfo* reflection =
			pipelineStateCache_.FindGraphicsReflection(drawPass->pipeline, drawPass->shaderOverride);
		if (reflection) {
			return reflection;
		}
	}

	const MaterialPassBinding* transparentPass = FindPass(material, MaterialPassKind::Transparent);
	if (transparentPass && transparentPass->pipeline) {
		return pipelineStateCache_.FindGraphicsReflection(transparentPass->pipeline, transparentPass->shaderOverride);
	}
	return nullptr;
}

const Engine::ShaderReflectionInfo* RenderPipelineRunner::FindMaterialRayTracingReflection(
	GraphicsCore& graphicsCore, AssetID materialAssetID) {

	const MaterialAsset* material = renderAssetLibrary_.LoadMaterial(materialAssetID);
	if (!material) {
		return nullptr;
	}
	const MaterialPassBinding* pass = FindPass(*material, MaterialPassKind::RayTracing);
	if (!pass || !pass->pipeline || pass->preferredVariant != PipelineVariantKind::Raytracing) {
		return nullptr;
	}
	RaytracingPipelineState* pipeline = raytracingPipelineStateCache_.GetOrCreate(
		graphicsCore.GetDXObject(), renderAssetLibrary_, pass->pipeline, pass->shaderOverride);
	return pipeline ? &pipeline->GetReflection() : nullptr;
}

bool Engine::RenderPipelineRunner::TryGetMaterialComputeReflection(GraphicsCore& graphicsCore, AssetID materialAssetID,
	MaterialPassKind passKind, std::vector<ShaderConstantBufferVariable>& outVariables,
	std::vector<ShaderResourceBinding>& outResources, std::vector<ShaderResourceBinding>& outSamplers) {

	return postProcessExecutor_.TryGetReflection(graphicsCore, renderAssetLibrary_, pipelineStateCache_, materialAssetID,
		passKind, outVariables, outResources, outSamplers);
}

Engine::DepthTexture2D* RenderPipelineRunner::GetViewDepthTexture(RenderViewKind kind) {

	// 深度はGBufferの色ではなくSceneMainの深度アタッチメントを参照する
	RenderPathResources& resources = FindCameraState(kind).resources;
	MultiRenderTarget* sceneMain = resources.GetSceneMain();
	return sceneMain ? sceneMain->GetDepthTexture() : nullptr;
}

void RenderPipelineRunner::Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	// プレビュー資源はframe境界で一度だけ初期化する
	previewResources_.previewBackendFrameStarted_ = false;

	// ワールドがない場合は描画できないので処理しない
	if (!request.world) {
		pickingState_.lastRenderRequest_ = {};
		pickingState_.lastActiveScene_ = nullptr;
		return;
	}

	// データクリア
	pickingState_.tlasResource_ = nullptr;
	pickingState_.pickRecords_.clear();
	pickingState_.pickRecordOffsets_.clear();

	// アセットライブラリの初期化、フレーム開始処理
	renderAssetLibrary_.Init(request.assetDatabase);
	postProcessAssetGenerator_.EnsureBuiltinAssets(request.assetDatabase);
	postProcessExecutor_.BeginFrame(request.systemContext->unscaledDeltaTime);
	rayTracingExecutor_.BeginFrame();
	colorPipelineProcessor_.BeginFrame();

	// World切替前のGPU処理を待ってbatchを破棄する
	if (request.world != lastRenderedWorld_) {
		if (lastRenderedWorld_) {
			graphicsCore.GetDXObject().WaitForGPU();
		}
		if (meshBackend_) {
			meshBackend_->ClearWorldBatchCaches();
		}
		if (previewResources_.previewMeshBackend_) {
			previewResources_.previewMeshBackend_->ClearWorldBatchCaches();
		}
		lastRenderedWorld_ = request.world;
	}
	backendRegistry_.BeginFrame(graphicsCore);

	// レイトレシーンフレーム開始処理
	raytracingSceneBuilder_.BeginFrame(graphicsCore);

	// Camera出力先を確定してから必要なサーフェイスをGPUと同期する
	ResolveViews(request);
	SyncRequestedSurfaces(graphicsCore, request);

	// デスクリプタヒープの一括設定
	graphicsCore.GetDXObject().GetDxCommand()->SetDescriptorHeaps({graphicsCore.GetSRVDescriptor().GetDescriptorHeap()});

	// GPU計測を開始し、前frameの結果を反映する
	GPUFrameProfiler::GetInstance().BeginFrame(graphicsCore.GetDXObject().GetDevice(),
		graphicsCore.GetDXObject().GetCommandQueue()->GetQueue(), graphicsCore.GetDXObject().GetResourceRetirement());

	// 描画アイテムの抽出
	scenePreparation_.Extract(*request.world, extractorRegistry_, lightExtractorRegistry_, &assetReloadService_);

	// アクティブなシーンインスタンスの取得
	const SceneInstance* activeScene = nullptr;
	if (request.sceneInstances) {
		activeScene = request.sceneInstances->Find(request.activeSceneInstanceID);
		if (!activeScene) {
			activeScene = request.sceneInstances->GetActive();
		}
	}
	pickingState_.lastRenderRequest_ = request;
	pickingState_.lastActiveScene_ = activeScene;

	// メッシュ描画クラスの取得(Initでキャッシュ済み)
	MeshRenderBackend* meshBackend = meshBackend_;

	// 一時領域の容量を保ってMesh要求を組み立てる
	scenePreparation_.RequestMeshes(
		graphicsCore, request.assetDatabase, meshBackend, activeScene, gameCameraViews_, sceneViewState_.view);

	// スクリプトのScreenPointToRay用にGameViewカメラのスナップショットを更新する
	GameViewCameraSnapshot::Snapshot cameraSnapshot{};
	if (gameViewState_.view.valid) {
		const ResolvedCameraView* gameCamera = gameViewState_.view.FindCamera(RenderCameraDomain::Perspective);
		if (!gameCamera) {
			gameCamera = gameViewState_.view.FindCamera(RenderCameraDomain::Orthographic);
		}
		if (gameCamera && gameCamera->valid) {

			cameraSnapshot.viewProjection = gameCamera->matrices.viewProjectionMatrix;
			cameraSnapshot.inverseViewProjection =
				gameCamera->matrices.inverseProjectionMatrix * gameCamera->matrices.inverseViewMatrix;
			cameraSnapshot.cameraPos = gameCamera->cameraPos;
			cameraSnapshot.width = static_cast<float>(gameViewState_.view.width);
			cameraSnapshot.height = static_cast<float>(gameViewState_.view.height);
			cameraSnapshot.valid = true;
		}
	}
	GameViewCameraSnapshot::Set(cameraSnapshot);

	// 描画ビューごとに描画を実行
	auto renderView = [&](RenderViewKind kind, const ResolvedRenderView& view, bool clearDefaultSurface) {
		const RenderViewRequest* viewRequest = request.FindView(kind);
		if (!view.valid || !viewRequest || !viewRequest->renderThisFrame) {
			return;
		}

		RenderPipelineViewResources& viewState = GetCameraState(view);
		GPUFrameProfiler::GetInstance().SetViewID(view.GetHistoryKey());
		viewState.EnsureLightBuffers(graphicsCore);
		viewState.EnsureRaytracingBuffers(graphicsCore);
		viewState.lightSet.Clear();
		if (activeScene) {
			ViewLightCollector::CollectForView(scenePreparation_.frameLightBatch_, activeScene, view, viewState.lightSet);
		}
		viewState.lightBuffers.Upload(viewState.lightSet);
		// 反射の背景も描画Cameraのレイヤーから解決する
		const SceneSkyboxInfo skyboxInfo = SceneSkyboxResolver::Resolve(
			graphicsCore, request.assetDatabase, request.world, view.GetCullingMask(RenderCameraDomain::Perspective));
		viewState.raytracingBuffers.Upload(view, skyboxInfo);

		SceneExecutionContext context = BuildViewExecutionContext(graphicsCore, request, activeScene, kind, view);
		if (!context.sceneInstance) {
			return;
		}
		// 出力先を同じ描画の入力から除外
		RuntimeTextureResolver::BeginRenderTextureWrite(view.targetTexture);
		context.clearDefaultSurface = clearDefaultSurface;
		// 描画対象をPassごとのbucketへ振り分ける
		RenderPassItemCollector::BuildBucketsForViewAndScene(
			scenePreparation_.renderBatch_, view, context.sceneInstance->instanceID, passBuckets_);

		ID3D12GraphicsCommandList6* commandList = graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
		const std::string viewName = std::string(EnumAdapter<RenderViewKind>::ToStringView(kind));

		// スキニングメッシュの頂点更新
		if (meshBackend) {
			GPUFrameProfiler::GetInstance().BeginPass(commandList, viewName + "/Skinning");
			PreDispatchSceneMeshSkinning(graphicsCore, context, scenePreparation_.renderBatch_, backendRegistry_,
				renderAssetLibrary_, pipelineStateCache_, materialResolver_);
			GPUFrameProfiler::GetInstance().EndPass(commandList);

			// 描画で使う実行ContextへTLASとBufferを登録する
			// 描画先のCameraでLODとBillboardを解決する
			PrimitiveGeometryManager* primitiveGeometryManager =
				primitiveBackend_ ? &primitiveBackend_->GetGeometryManager() : nullptr;
			GPUFrameProfiler::GetInstance().BeginPass(commandList, viewName + "/RaytracingSceneBuild");
			raytracingSceneBuilder_.BuildForScene(graphicsCore, *request.assetDatabase, renderAssetLibrary_, materialResolver_,
				meshBackend, primitiveGeometryManager, scenePreparation_.renderBatch_, context);
			GPUFrameProfiler::GetInstance().EndPass(commandList);

			if (context.raytracing.tlasResource) {
				pickingState_.tlasResource_ = context.raytracing.tlasResource;
				pickingState_.pickRecords_ = raytracingSceneBuilder_.GetPickRecords();
				pickingState_.pickRecordOffsets_ = raytracingSceneBuilder_.GetPickRecordOffsets();
			}
		}

		// TLASバッファをリソースレジストリに登録
		if (context.raytracing.tlasResource) {

			context.bufferRegistry.Register({.alias = "SceneTLAS",
				.resource = context.raytracing.tlasResource,
				.gpuAddress = context.raytracing.tlasResource->GetGPUVirtualAddress(),
				.srvGPUHandle = {},
				.uavGPUHandle = {},
				.elementCount = context.raytracing.instanceCount,
				.stride = 0});
			context.bufferRegistry.Register({.alias = "gSceneTLAS",
				.resource = context.raytracing.tlasResource,
				.gpuAddress = context.raytracing.tlasResource->GetGPUVirtualAddress(),
				.srvGPUHandle = {},
				.uavGPUHandle = {},
				.elementCount = context.raytracing.instanceCount,
				.stride = 0});
		}

		// 固定RenderPathを実行
		renderPath_.Execute(graphicsCore, passBuckets_, context);

		// 終了後に全ターゲットをシェーダーリード状態へ遷移
		auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
		for (MultiRenderTarget* surface : context.targetRegistry->GatherUniqueSurfaces()) {
			surface->TransitionForShaderRead(*dxCommand);
		}
		if (context.resources) {
			if (context.resources->GetSceneMain()) {
				context.resources->GetSceneMain()->TransitionForShaderRead(*dxCommand);
			}
			if (context.resources->GetSceneFinal()) {
				context.resources->GetSceneFinal()->TransitionForShaderRead(*dxCommand);
			}
		}
		RuntimeTextureResolver::EndRenderTextureWrite(view.targetTexture);
	};
	std::unordered_set<AssetID> clearedRenderTextures{};
	bool clearGameSurface = true;
	for (const ResolvedRenderView& gameView : gameCameraViews_) {
		if (!gameView.valid) {
			continue;
		}
		bool clearSurface = clearGameSurface;
		if (gameView.targetTexture) {
			clearSurface = clearedRenderTextures.emplace(gameView.targetTexture).second;
		} else {
			clearGameSurface = false;
		}
		renderView(RenderViewKind::Game, gameView, clearSurface);
	}
	if (gameCameraViews_.empty()) {
		renderView(RenderViewKind::Game, gameViewState_.view, true);
	}
	renderView(RenderViewKind::Scene, sceneViewState_.view, true);

	// 記録したパスのタイムスタンプを解決してリードバックバッファへ書き出す
	GPUFrameProfiler::GetInstance().Resolve(graphicsCore.GetDXObject().GetDxCommand()->GetCommandList());
}

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================

namespace Engine {

	const ShaderReflectionInfo* RenderPipelineRunner::FindPipelineGraphicsReflection(AssetID pipelineAssetID) const {

		return pipelineStateCache_.FindGraphicsReflection(pipelineAssetID);
	}

	const PerViewLightSet& RenderPipelineRunner::GetResolvedViewLightSet(RenderViewKind kind) const {

		return FindCameraState(kind).lightSet;
	}
}
