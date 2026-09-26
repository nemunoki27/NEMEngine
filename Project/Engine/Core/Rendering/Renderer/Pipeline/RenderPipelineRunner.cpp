#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include "RuntimeRenderPreloader.h"
#include "RenderPipelineUtility.h"
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Renderer/Views/GameViewCameraSnapshot.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/BuiltinLightExtractors.h>
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

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================

void RenderPipelineRunner::Init() {

	viewportRenderService_ = std::make_unique<ViewportRenderService>();

	// 描画アイテム抽出器の登録
	extractorRegistry_.Clear();
	extractorRegistry_.Register(std::make_unique<SpriteRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<TextRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<MeshRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<LineRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<PrimitiveRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<ParticleRenderItemExtractor>());
	// 描画バックエンドの登録
	backendRegistry_.Clear();
	backendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	backendRegistry_.Register(std::make_unique<TextRenderBackend>());
	backendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	backendRegistry_.Register(std::make_unique<LineRenderBackend>());
	backendRegistry_.Register(std::make_unique<PrimitiveRenderBackend>());
	backendRegistry_.Register(std::make_unique<ParticleRenderBackend>());
	// ツールプレビューはメインビューとは別のGPUバッファを持たせる
	previewResources_.previewBackendRegistry_.Clear();
	previewResources_.previewBackendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<TextRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<LineRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<PrimitiveRenderBackend>());
	previewResources_.previewBackendRegistry_.Register(std::make_unique<ParticleRenderBackend>());
	// 型付きMeshバックエンドをキャッシュして毎フレームのdynamic_castを避ける
	meshBackend_ = dynamic_cast<MeshRenderBackend*>(backendRegistry_.Find(RenderBackendID::Mesh));
	previewResources_.previewMeshBackend_ = dynamic_cast<MeshRenderBackend*>(previewResources_.previewBackendRegistry_.Find(RenderBackendID::Mesh));
	primitiveBackend_ = dynamic_cast<PrimitiveRenderBackend*>(backendRegistry_.Find(RenderBackendID::Primitive));
	particleBackend_ = dynamic_cast<ParticleRenderBackend*>(backendRegistry_.Find(RenderBackendID::Particle));
	// ライト抽出器の登録
	lightExtractorRegistry_.Clear();
	lightExtractorRegistry_.Register(std::make_unique<DirectionalLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<PointLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<RectLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<SpotLightExtractor>());

	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	postProcessExecutor_.Release();
	rayTracingExecutor_.Release();
	colorPipelineProcessor_.Release();
	postProcessAssetGenerator_.Clear();
	scenePreparation_.frameLightBatch_.Clear();
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	previewResources_.previewLightSet_.Clear();

	raytracingPipelineStateCache_.Clear();
	gameViewState_.raytracingBuffers.Release();
	sceneViewState_.raytracingBuffers.Release();

	// 固定RenderPathの初期化
	{
		RenderPipelineDeps deps{};
		deps.renderBatch = &scenePreparation_.renderBatch_;
		deps.backendRegistry = &backendRegistry_;
		deps.assetLibrary = &renderAssetLibrary_;
		deps.pipelineCache = &pipelineStateCache_;
		deps.materialResolver = &materialResolver_;
		deps.raytracingPipelineCache = &raytracingPipelineStateCache_;
		deps.rayTracingExecutor = &rayTracingExecutor_;
		deps.postProcessExecutor = &postProcessExecutor_;
		deps.postProcessTargetPool = &postProcessTargetPool_;
		deps.postProcessDebugInjector = &postProcessDebugInjector_;
		deps.postProcessAssetGenerator = &postProcessAssetGenerator_;
		deps.colorPipelineProcessor = &colorPipelineProcessor_;
		deps.dispatcher = &batchDispatcher_;
		renderPath_.Initialize(deps);
	}

	// ビューライトバッファの初期化
	gameViewState_.lightBuffers.Release();
	sceneViewState_.lightBuffers.Release();
	previewResources_.previewLightBufferPool_.Clear();
	previewResources_.previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
}

void RenderPipelineRunner::PreloadRuntimeAssets(GraphicsCore& graphicsCore, AssetDatabase& assetDatabase) {

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
	RuntimeRenderPreloader::Preload(graphicsCore, assetDatabase, context);
}

void RenderPipelineRunner::ReloadMesh(AssetID meshAssetID) {

	// 本番用とプレビュー用の両メッシュバックエンドへ再ロードを伝える
	if (meshBackend_) {
		meshBackend_->RequestMeshReload(meshAssetID);
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

bool RenderPipelineRunner::ReloadMaterialDependencies(
	AssetDatabase& assetDatabase, AssetID materialAssetID) {

	return assetReloadService_.ReloadMaterialDependencies(assetDatabase, materialAssetID);
}

void RenderPipelineRunner::ReloadAsset(AssetDatabase& assetDatabase, AssetID assetID) {

	assetReloadService_.ReloadAsset(assetDatabase, assetID);
}

Engine::RenderTexture2D* RenderPipelineRunner::GetViewGBufferTexture(RenderViewKind kind, GBufferAttachment attachment) {

	// GameViewはgameViewState_.resources、それ以外はsceneViewState_.resourcesのGBufferを参照する
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewState_.resources : sceneViewState_.resources;
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
	if (!pass || !pass->pipeline ||
		pass->preferredVariant != PipelineVariantKind::Raytracing) {
		return nullptr;
	}
	RaytracingPipelineState* pipeline = raytracingPipelineStateCache_.GetOrCreate(
		graphicsCore.GetDXObject(), renderAssetLibrary_,
		pass->pipeline, pass->shaderOverride);
	return pipeline ? &pipeline->GetReflection() : nullptr;
}

bool Engine::RenderPipelineRunner::TryGetMaterialComputeReflection(
	GraphicsCore& graphicsCore, AssetID materialAssetID,
	MaterialPassKind passKind,
	std::vector<ShaderConstantBufferVariable>& outVariables,
	std::vector<ShaderResourceBinding>& outResources,
	std::vector<ShaderResourceBinding>& outSamplers) {

	return postProcessExecutor_.TryGetReflection(graphicsCore,
		renderAssetLibrary_, pipelineStateCache_, materialAssetID, passKind,
		outVariables, outResources, outSamplers);
}

Engine::DepthTexture2D* RenderPipelineRunner::GetViewDepthTexture(RenderViewKind kind) {

	// 深度はGBufferの色ではなくSceneMainの深度アタッチメントを参照する
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewState_.resources : sceneViewState_.resources;
	MultiRenderTarget* sceneMain = resources.GetSceneMain();
	return sceneMain ? sceneMain->GetDepthTexture() : nullptr;
}

void RenderPipelineRunner::Finalize() {

	// GPU計測用のクエリヒープ/リードバックバッファはここで解放する
	// シングルトンのため放置するとDeviceより後まで生き残り、LeakCheckerに残る
	GPUFrameProfiler::GetInstance().Finalize();

	renderPath_.Finalize();
	backendRegistry_.Clear();
	previewResources_.previewBackendRegistry_.Clear();
	meshBackend_ = nullptr;
	primitiveBackend_ = nullptr;
	particleBackend_ = nullptr;
	previewResources_.previewMeshBackend_ = nullptr;
	extractorRegistry_.Clear();
	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	postProcessExecutor_.Release();
	rayTracingExecutor_.Release();
	colorPipelineProcessor_.Release();
	postProcessAssetGenerator_.Clear();
	lightExtractorRegistry_.Clear();
	scenePreparation_.frameLightBatch_.Clear();
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	previewResources_.previewLightSet_.Clear();
	gameViewState_.lightBuffers.Release();
	sceneViewState_.lightBuffers.Release();
	previewResources_.previewLightBufferPool_.Clear();
	assetReloadService_.ClearRenderStates();
	if (viewportRenderService_) {
		viewportRenderService_->Finalize();
		viewportRenderService_.reset();
	}
	raytracingPipelineStateCache_.Clear();
	gameViewState_.raytracingBuffers.Release();
	sceneViewState_.raytracingBuffers.Release();
	previewResources_.previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
	pickingState_.lastRenderRequest_ = {};
	pickingState_.lastActiveScene_ = nullptr;
	raytracingSceneBuilder_.Finalize();
	gameViewState_.resources.Destroy();
	sceneViewState_.resources.Destroy();
}

void RenderPipelineRunner::Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	// エディタPostSceneで複数のプレビューを描画するため、プレビューbackendのリソースプールは
	// ここでフレーム境界だけリセットし、RenderEntityPreviewごとにはリセットしない
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

	// World切替前の描画が参照中のGPUリソースを解放しないよう完了を待ってからキャッシュを破棄する
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

	// 描画要求に基づいて必要なサーフェイスをGPUと同期し、ビュー情報を決定
	SyncRequestedSurfaces(graphicsCore, request);
	ResolveViews(request);

	// デスクリプタヒープの一括設定
	graphicsCore.GetDXObject().GetDxCommand()->SetDescriptorHeaps({
		graphicsCore.GetSRVDescriptor().GetDescriptorHeap()
		});

	// GPU計測のフレーム開始(前フレームの結果をFrameProfilerへ反映し、記録をリセット)
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

	// シーン切り替え時に統合RenderFeatureProfileをサービスへ通知する
	if (activeScene) {

		const AssetID profileAsset =
			activeScene->header.renderFeatureProfile;
		if (profileAsset != lastNotifiedRenderFeatureProfile_) {

			RenderFeatureProfileService& service =
				RenderFeatureProfileService::GetInstance();
			if (!service.IsDirty()) {
				service.SetActiveProfileAsset(
					profileAsset, request.assetDatabase);
			}
			lastNotifiedRenderFeatureProfile_ = profileAsset;
		}
	}

	// メッシュ描画クラスの取得(Initでキャッシュ済み)
	MeshRenderBackend* meshBackend = meshBackend_;

	// 毎フレーム使い回すスクラッチをクリアする(容量は保持して再確保を避ける)
	scenePreparation_.RequestMeshes(graphicsCore, request.assetDatabase, meshBackend, activeScene,
		gameViewState_.view, sceneViewState_.view);

	// ビューごとのライト集合クリア
	gameViewState_.lightSet.Clear();
	sceneViewState_.lightSet.Clear();
	// ルートシーン用のビューライト構築
	if (activeScene) {
		if (gameViewState_.view.valid) {

			ViewLightCollector::CollectForView(scenePreparation_.frameLightBatch_, activeScene, gameViewState_.view, gameViewState_.lightSet);
		}
		if (sceneViewState_.view.valid) {

			ViewLightCollector::CollectForView(scenePreparation_.frameLightBatch_, activeScene, sceneViewState_.view, sceneViewState_.lightSet);
		}
	}

	// GPUライトバッファ初期化
	gameViewState_.EnsureLightBuffers(graphicsCore);
	sceneViewState_.EnsureLightBuffers(graphicsCore);
	// ビューごとのライト集合をGPUへ転送
	gameViewState_.lightBuffers.Upload(gameViewState_.lightSet);
	sceneViewState_.lightBuffers.Upload(sceneViewState_.lightSet);

	// レイトレーシングビュー関連バッファの初期化と転送
	gameViewState_.EnsureRaytracingBuffers(graphicsCore);
	sceneViewState_.EnsureRaytracingBuffers(graphicsCore);
	// 反射レイのミス時に参照するskyboxを解決して渡す
	const SceneSkyboxInfo skyboxInfo = SceneSkyboxResolver::Resolve(graphicsCore, request.assetDatabase, request.world);
	gameViewState_.raytracingBuffers.Upload(gameViewState_.view, skyboxInfo);
	sceneViewState_.raytracingBuffers.Upload(sceneViewState_.view, skyboxInfo);

	// スクリプトのScreenPointToRay用にGameViewカメラのスナップショットを更新する
	GameViewCameraSnapshot::Snapshot cameraSnapshot{};
	if (gameViewState_.view.valid) {
		if (const ResolvedCameraView* gameCamera = gameViewState_.view.FindCamera(RenderCameraDomain::Perspective);
			gameCamera && gameCamera->valid) {

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
	auto renderView = [&](RenderViewKind kind, const ResolvedRenderView& view) {
		const RenderViewRequest* viewRequest = request.FindView(kind);
		if (!view.valid || !viewRequest ||
			!viewRequest->renderThisFrame) {
			return;
		}

		SceneExecutionContext context = BuildViewExecutionContext(graphicsCore, request, activeScene, kind, view);
		if (!context.sceneInstance) {
			return;
		}
		// バケットはメンバを使い回して内部vectorの容量を保持する(BuildBucketsForViewAndScene内でClearされる)
		RenderPassItemCollector::BuildBucketsForViewAndScene(
			scenePreparation_.renderBatch_, view, context.sceneInstance->instanceID, passBuckets_);

		ID3D12GraphicsCommandList6* commandList =
			graphicsCore.GetDXObject().GetDxCommand()->GetCommandList();
		const std::string viewName = std::string(EnumAdapter<RenderViewKind>::ToStringView(kind));

		// スキニングメッシュの頂点更新
		if (meshBackend) {
			GPUFrameProfiler::GetInstance().BeginPass(commandList, viewName + "/Skinning");
			PreDispatchSceneMeshSkinning(graphicsCore, context,
				scenePreparation_.renderBatch_, backendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_);
			GPUFrameProfiler::GetInstance().EndPass(commandList);

			// レイトレーシングシーンの構築
			// gRaytracingSceneInstances/gRaytracingSubMeshesはcontext.bufferRegistryへ登録する必要があるため、
			// コピーではなく実際のcontextへ直接構築する、コピーへ構築すると登録が破棄され反射パスが早期リターンする
			// TLAS構築の基準ビューだけ一時的にGameViewへ差し替え、構築後に元へ戻す
			const ResolvedRenderView* prevTlasView = context.view;
			if (gameViewState_.view.valid) {
				context.view = &gameViewState_.view;
			}
			PrimitiveGeometryManager* primitiveGeometryManager = primitiveBackend_ ? &primitiveBackend_->GetGeometryManager() : nullptr;
			GPUFrameProfiler::GetInstance().BeginPass(commandList, viewName + "/RaytracingSceneBuild");
			raytracingSceneBuilder_.BuildForScene(
				graphicsCore, *request.assetDatabase,
				renderAssetLibrary_, materialResolver_, meshBackend,
				primitiveGeometryManager, scenePreparation_.renderBatch_, context);
			GPUFrameProfiler::GetInstance().EndPass(commandList);
			context.view = prevTlasView;

			if (context.raytracing.tlasResource) {
				pickingState_.tlasResource_ = context.raytracing.tlasResource;
				pickingState_.pickRecords_ = raytracingSceneBuilder_.GetPickRecords();
				pickingState_.pickRecordOffsets_ = raytracingSceneBuilder_.GetPickRecordOffsets();
			}
		}

		// TLASバッファをリソースレジストリに登録
		if (context.raytracing.tlasResource) {

			context.bufferRegistry.Register({ .alias = "SceneTLAS",.resource = context.raytracing.tlasResource,
				.gpuAddress = context.raytracing.tlasResource->GetGPUVirtualAddress(),.srvGPUHandle = {},.uavGPUHandle = {},
				.elementCount = context.raytracing.instanceCount,.stride = 0 });
			context.bufferRegistry.Register({ .alias = "gSceneTLAS",.resource = context.raytracing.tlasResource,
				.gpuAddress = context.raytracing.tlasResource->GetGPUVirtualAddress(),.srvGPUHandle = {},.uavGPUHandle = {},
				.elementCount = context.raytracing.instanceCount,.stride = 0 });
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
		};
	renderView(RenderViewKind::Game, gameViewState_.view);
	renderView(RenderViewKind::Scene, sceneViewState_.view);

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

		return (kind == RenderViewKind::Game) ? gameViewState_.lightSet : sceneViewState_.lightSet;
	}
}
