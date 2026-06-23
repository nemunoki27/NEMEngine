#include "RenderPipelineRunner.h"
#include "RenderPipelineUtility.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Profiling/GPUFrameProfiler.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/Directional/DirectionalLightExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/Point/PointLightExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Builtin/Spot/SpotLightExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/ViewLightCollector.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/PostProcess/Stack/PostProcessStackService.h>

// c++
#include <algorithm>

#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

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
	// 描画バックエンドの登録
	backendRegistry_.Clear();
	backendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	backendRegistry_.Register(std::make_unique<TextRenderBackend>());
	backendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	backendRegistry_.Register(std::make_unique<LineRenderBackend>());
	// ツールプレビューはメインビューとは別のGPUバッファを持たせる
	previewBackendRegistry_.Clear();
	previewBackendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<TextRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<LineRenderBackend>());
	// 型付きMeshバックエンドをキャッシュして毎フレームのdynamic_castを避ける
	meshBackend_ = dynamic_cast<MeshRenderBackend*>(backendRegistry_.Find(RenderBackendID::Mesh));
	previewMeshBackend_ = dynamic_cast<MeshRenderBackend*>(previewBackendRegistry_.Find(RenderBackendID::Mesh));
	// ライト抽出器の登録
	lightExtractorRegistry_.Clear();
	lightExtractorRegistry_.Register(std::make_unique<DirectionalLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<PointLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<SpotLightExtractor>());

	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	postProcessExecutor_.Release();
	postProcessAssetGenerator_.Clear();
	frameLightBatch_.Clear();
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	previewLightSet_.Clear();

	raytracingPipelineStateCache_.Clear();
	gameViewRaytracingBuffers_.Release();
	sceneViewRaytracingBuffers_.Release();

	// 固定RenderPathの初期化
	{
		RenderPipelineDeps deps{};
		deps.renderBatch = &renderBatch_;
		deps.backendRegistry = &backendRegistry_;
		deps.assetLibrary = &renderAssetLibrary_;
		deps.pipelineCache = &pipelineStateCache_;
		deps.materialResolver = &materialResolver_;
		deps.raytracingPipelineCache = &raytracingPipelineStateCache_;
		deps.postProcessExecutor = &postProcessExecutor_;
		deps.postProcessTargetPool = &postProcessTargetPool_;
		deps.postProcessDebugInjector = &postProcessDebugInjector_;
		deps.postProcessAssetGenerator = &postProcessAssetGenerator_;
		deps.dispatcher = &batchDispatcher_;
		renderPath_.Initialize(deps);
	}

	// ビューライトバッファの初期化
	gameViewLightBuffers_.Release();
	sceneViewLightBuffers_.Release();
	previewLightBufferPool_.Clear();
	previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
}

void RenderPipelineRunner::ReloadMesh(AssetID meshAssetID) {

	// 本番用とプレビュー用の両メッシュバックエンドへ再ロードを伝える
	if (meshBackend_) {
		meshBackend_->RequestMeshReload(meshAssetID);
	}
	if (previewMeshBackend_) {
		previewMeshBackend_->RequestMeshReload(meshAssetID);
	}
}

void RenderPipelineRunner::ReloadMaterial(AssetID materialAssetID) {

	// マテリアルキャッシュを破棄して次フレームのLoadMaterialでファイルから読み直させる
	// インスペクタでの編集を実行中に即反映するため
	renderAssetLibrary_.InvalidateMaterial(materialAssetID);
}

Engine::RenderTexture2D* RenderPipelineRunner::GetViewGBufferTexture(RenderViewKind kind, GBufferAttachment attachment) {

	// GameViewはgameViewResources_、それ以外はsceneViewResources_のGBufferを参照する
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewResources_ : sceneViewResources_;
	return resources.GetGBuffer(attachment);
}

const Engine::ShaderReflectionInfo* RenderPipelineRunner::FindMaterialDrawReflection(const MaterialAsset& material) const {

	const MaterialPassBinding* drawPass = FindPass(material, MaterialPassKind::Draw);
	if (!drawPass || !drawPass->pipeline) {
		return nullptr;
	}
	return FindPipelineGraphicsReflection(drawPass->pipeline);
}

Engine::DepthTexture2D* RenderPipelineRunner::GetViewDepthTexture(RenderViewKind kind) {

	// 深度はGBufferの色ではなくSceneMainの深度アタッチメントを参照する
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewResources_ : sceneViewResources_;
	MultiRenderTarget* sceneMain = resources.GetSceneMain();
	return sceneMain ? sceneMain->GetDepthTexture() : nullptr;
}

void RenderPipelineRunner::Finalize() {

	// GPU計測用のクエリヒープ/リードバックバッファはここで解放する
	// シングルトンのため放置するとDeviceより後まで生き残り、LeakCheckerに残る
	GPUFrameProfiler::GetInstance().Finalize();

	renderPath_.Finalize();
	backendRegistry_.Clear();
	previewBackendRegistry_.Clear();
	meshBackend_ = nullptr;
	previewMeshBackend_ = nullptr;
	extractorRegistry_.Clear();
	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	postProcessExecutor_.Release();
	postProcessAssetGenerator_.Clear();
	lightExtractorRegistry_.Clear();
	frameLightBatch_.Clear();
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	previewLightSet_.Clear();
	gameViewLightBuffers_.Release();
	sceneViewLightBuffers_.Release();
	previewLightBufferPool_.Clear();
	if (viewportRenderService_) {
		viewportRenderService_->Finalize();
		viewportRenderService_.reset();
	}
	raytracingPipelineStateCache_.Clear();
	gameViewRaytracingBuffers_.Release();
	sceneViewRaytracingBuffers_.Release();
	previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
	raytracingSceneBuilder_.Finalize();
	gameViewResources_.Destroy();
	sceneViewResources_.Destroy();
}

void RenderPipelineRunner::Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {


	// エディタPostSceneで複数のプレビューを描画するため、プレビューbackendのリソースプールは
	// ここでフレーム境界だけリセットし、RenderEntityPreviewごとにはリセットしない
	previewBackendFrameStarted_ = false;

	// ワールドがない場合は描画できないので処理しない
	if (!request.world) {
		return;
	}

	// データクリア
	tlasResource_ = nullptr;
	pickRecords_.clear();

	// アセットライブラリの初期化、フレーム開始処理
	renderAssetLibrary_.Init(request.assetDatabase);
	postProcessAssetGenerator_.EnsureBuiltinAssets(request.assetDatabase);
	postProcessExecutor_.BeginFrame(request.systemContext ? request.systemContext->deltaTime : 0.0f);
	backendRegistry_.BeginFrame(graphicsCore);

	// ワールドが切り替わった場合は静的バッチキャッシュを即時破棄してSRV重複確保を防ぐ
	if (request.world != lastRenderedWorld_) {
		if (meshBackend_) {
			meshBackend_->ClearStaticBatchCache();
		}
		if (previewMeshBackend_) {
			previewMeshBackend_->ClearStaticBatchCache();
		}
		lastRenderedWorld_ = request.world;
	}

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
		graphicsCore.GetDXObject().GetDxCommand()->GetQueue());

	// 描画アイテムの抽出
	extractorRegistry_.BuildBatch(*request.world, renderBatch_);
	// ライト抽出
	lightExtractorRegistry_.BuildBatch(*request.world, frameLightBatch_);

	// アクティブなシーンインスタンスの取得
	const SceneInstance* activeScene = nullptr;
	if (request.sceneInstances) {
		activeScene = request.sceneInstances->Find(request.activeSceneInstanceID);
		if (!activeScene) {
			activeScene = request.sceneInstances->GetActive();
		}
	}

	// シーン切り替え時にPostProcessStack設定をサービスへ通知する
	if (activeScene) {

		const AssetID ppAsset = activeScene->header.postProcessStack;
		if (ppAsset != lastNotifiedPostProcessStack_) {

			PostProcessStackService& service = PostProcessStackService::GetInstance();
			if (!service.IsDirty()) {
				service.SetActiveSettingsAsset(ppAsset, request.assetDatabase);
			}
			lastNotifiedPostProcessStack_ = ppAsset;
		}
	}

	// メッシュ描画クラスの取得(Initでキャッシュ済み)
	MeshRenderBackend* meshBackend = meshBackend_;

	// 毎フレーム使い回すスクラッチをクリアする(容量は保持して再確保を避ける)
	visibleMeshSet_.clear();
	visibleMeshSet_.reserve(renderBatch_.GetItems().size());

	// ビューごとに可視なメッシュアセットIDを収集
	if (meshBackend && activeScene) {
		if (gameView_.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, gameView_, visibleMeshSet_);
		}
		if (sceneView_.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, sceneView_, visibleMeshSet_);
		}
	}

	visibleMeshes_.clear();
	visibleMeshes_.reserve(visibleMeshSet_.size());
	for (const AssetID& id : visibleMeshSet_) {
		visibleMeshes_.emplace_back(id);
	}
	// GPUに可視なメッシュの情報を要求して、必要なリソースを準備
	if (meshBackend && !visibleMeshes_.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, visibleMeshes_);
	}

	// ビューごとのライト集合クリア
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	const bool sceneViewSharesGameLightBuffers = sceneView_.valid && gameView_.valid;
	// ルートシーン用のビューライト構築
	if (activeScene) {
		if (gameView_.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, gameView_, gameViewLightSet_);
		}
		if (sceneView_.valid && !sceneViewSharesGameLightBuffers) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, sceneView_, sceneViewLightSet_);
		}
	}

	// GPUライトバッファ初期化
	if (!gameViewLightBuffers_.IsInitialized()) {
		gameViewLightBuffers_.Init(graphicsCore);
	}
	if (!sceneViewSharesGameLightBuffers && !sceneViewLightBuffers_.IsInitialized()) {
		sceneViewLightBuffers_.Init(graphicsCore);
	}
	// ビューごとのライト集合をGPUへ転送
	gameViewLightBuffers_.Upload(gameViewLightSet_);
	if (!sceneViewSharesGameLightBuffers) {

		sceneViewLightBuffers_.Upload(sceneViewLightSet_);
	}

	// レイトレーシングビュー関連バッファの初期化と転送
	if (!gameViewRaytracingBuffers_.IsInitialized()) {
		gameViewRaytracingBuffers_.Init(graphicsCore);
	}
	if (!sceneViewRaytracingBuffers_.IsInitialized()) {
		sceneViewRaytracingBuffers_.Init(graphicsCore);
	}
	gameViewRaytracingBuffers_.Upload(gameView_);
	sceneViewRaytracingBuffers_.Upload(sceneView_);

	// 描画ビューごとに描画を実行
	auto renderView = [&](RenderViewKind kind, const ResolvedRenderView& view) {
		if (!view.valid) {
			return;
		}

		SceneExecutionContext context = BuildViewExecutionContext(graphicsCore, request, activeScene, kind, view);
		if (!context.sceneInstance) {
			return;
		}
		// バケットはメンバを使い回して内部vectorの容量を保持する(BuildBucketsForViewAndScene内でClearされる)
		RenderPassItemCollector::BuildBucketsForViewAndScene(
			renderBatch_, view, context.sceneInstance->instanceID, passBuckets_);

		// スキニングメッシュの頂点更新
		if (meshBackend) {
			PreDispatchVisibleMeshSkinning(graphicsCore, context,
				renderBatch_, backendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets_);

			// レイトレーシングシーンの構築
			// gRaytracingSceneInstances/gRaytracingSubMeshesはcontext.bufferRegistryへ登録する必要があるため、
			// コピーではなく実際のcontextへ直接構築する、コピーへ構築すると登録が破棄され反射パスが早期リターンする
			// TLAS構築の基準ビューだけ一時的にGameViewへ差し替え、構築後に元へ戻す
			const ResolvedRenderView* prevTlasView = context.view;
			if (gameView_.valid) {
				context.view = &gameView_;
			}
			raytracingSceneBuilder_.BuildForScene(graphicsCore, *request.assetDatabase, meshBackend, renderBatch_, context);
			context.view = prevTlasView;

			if (context.raytracing.tlasResource) {
				tlasResource_ = context.raytracing.tlasResource;
				pickRecords_ = raytracingSceneBuilder_.GetPickRecords();
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
	renderView(RenderViewKind::Game, gameView_);
	renderView(RenderViewKind::Scene, sceneView_);

	// 記録したパスのタイムスタンプを解決してリードバックバッファへ書き出す
	GPUFrameProfiler::GetInstance().Resolve(graphicsCore.GetDXObject().GetDxCommand()->GetCommandList());
}

bool RenderPipelineRunner::PresentViewToBackBuffer(
	GraphicsCore& graphicsCore, RenderViewKind kind, AssetID material) {

	// 指定された種類の描画ビューのサーフェスを取得
	MultiRenderTarget* source = viewportRenderService_->GetSurface(kind);
	AssetDatabase* assetDatabase = renderAssetLibrary_.GetDatabase();
	if (!source || !source->GetColorTexture(0) || !assetDatabase) {
		return false;
	}

	// フルスクリーンコピー用のマテリアルを取得して読み込む
	AssetID resolvedMaterialID = materialResolver_.ResolveORDefault(*assetDatabase, material, DefaultMaterialSlot::FullscreenCopy);
	const MaterialAsset* materialAsset = renderAssetLibrary_.LoadMaterial(resolvedMaterialID);
	if (!materialAsset) {
		return false;
	}

	// ブリットパスかフルスクリーンパスを探す
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, MaterialPassKind::Blit);
	if (!passBinding) {
		passBinding = FindPass(*materialAsset, MaterialPassKind::Fullscreen);
	}
	// 無効なパスは処理しない
	if (!passBinding || passBinding->preferredVariant == PipelineVariantKind::Compute ||
		passBinding->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	// バックバッファのフォーマットに合わせたパイプラインステートを取得
	std::vector<DXGI_FORMAT> rtvFormats = {
		graphicsCore.GetBackBufferRenderTarget().format
	};
	const PipelineState* pipelineState = pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(),
		renderAssetLibrary_, passBinding->pipeline, passBinding->preferredVariant, rtvFormats, DXGI_FORMAT_UNKNOWN);
	if (!pipelineState || !pipelineState->GetGraphicsPipeline(BlendMode::Normal)) {
		return false;
	}

	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	auto* commandList = dxCommand->GetCommandList();

	// ソースをシェーダーリード状態に遷移
	source->TransitionForShaderRead(*dxCommand);

	dxCommand->SetDescriptorHeaps({ graphicsCore.GetSRVDescriptor().GetDescriptorHeap() });

	// パイプラインを設定
	commandList->SetGraphicsRootSignature(pipelineState->GetRootSignature());
	commandList->SetPipelineState(pipelineState->GetGraphicsPipeline(BlendMode::Normal));

	// サーフェイスを設定
	if (const RootBindingLocation* sourceColorBinding = pipelineState->FindBinding(ShaderBindingKind::SRV, 0, 0)) {

		commandList->SetGraphicsRootDescriptorTable(sourceColorBinding->rootParameterIndex, source->GetColorTexture(0)->GetSRVGPUHandle());
	}

	// バックバッファをレンダーターゲットとしてバインド
	dxCommand->BindRenderTargets(std::optional<RenderTarget>(graphicsCore.GetBackBufferRenderTarget()), std::nullopt);
	dxCommand->SetViewportAndScissor(static_cast<uint32_t>(graphicsCore.GetSwapChainDesc().Width), static_cast<uint32_t>(graphicsCore.GetSwapChainDesc().Height));

	// 全画面三角形を描画
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);

	return true;
}

// RenderEntityPreviewの実装はRenderPipelineRunnerPreview.cppへ分離

void RenderPipelineRunner::SyncRequestedSurfaces(
	GraphicsCore& graphicsCore, const RenderFrameRequest& request) {


	for (const auto& viewRequest : request.views) {
		if (!viewRequest.enabled) {
			continue;
		}
		viewportRenderService_->SyncSurface(graphicsCore, viewRequest.kind, viewRequest.width, viewRequest.height);
	}
}

void RenderPipelineRunner::ResolveViews(const RenderFrameRequest& request) {

	gameView_ = {};
	sceneView_ = {};
	for (const auto& viewRequest : request.views) {

		ResolvedRenderView resolved = RenderViewResolver::Resolve(viewRequest, *request.world);
		switch (viewRequest.kind) {
		case RenderViewKind::Game:

			gameView_ = resolved;
			break;
		case RenderViewKind::Scene:

			sceneView_ = resolved;
			break;
		}
	}
}

SceneExecutionContext RenderPipelineRunner::BuildViewExecutionContext(GraphicsCore& graphicsCore,
	const RenderFrameRequest& request, const SceneInstance* sceneInstance,
	RenderViewKind kind, const ResolvedRenderView& view) {


	// コンテキストの構築
	SceneExecutionContext context{};
	context.kind = kind;
	context.sceneInstance = sceneInstance;
	context.view = &view;
	// SceneViewの描画カメラはSceneViewのまま、カリングだけGameView基準にする
	context.cullingView = (kind == RenderViewKind::Scene && gameView_.valid) ? &gameView_ : &view;
	context.defaultSurface = viewportRenderService_->GetSurface(kind);
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;
	context.requireRaytracingSceneForEditorPicking = request.requireRaytracingSceneForEditorPicking;
	context.drawSceneViewDefaultGrid = request.drawSceneViewDefaultGrid;
	context.allowSceneComponentOverlay = (kind == RenderViewKind::Scene);
	// 種類に応じたターゲットレジストリを選択
	RenderTargetRegistry* registry = kind == RenderViewKind::Game ?
		&gameViewTargetRegistry_ : &sceneViewTargetRegistry_;
	context.targetRegistry = registry;

	// フレーム開始処理
	registry->BeginFrame();

	// デフォルトのサーフェイスがある場合はレジストリに登録
	if (context.defaultSurface) {

		std::string colorName = ViewportRenderService::GetPrimaryColorName(kind);
		std::optional<std::string> depthName = std::string(ViewportRenderService::GetPrimaryDepthName(kind));
		registry->Register("View", context.defaultSurface, { colorName }, depthName);
		registry->Register(ViewportRenderService::GetViewAlias(kind), context.defaultSurface, { colorName }, depthName);
	}

	// ビューごとの中間レンダーターゲットを確保してコンテキストに設定
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewResources_ : sceneViewResources_;
	resources.Resize(graphicsCore, view.width, view.height);
	context.resources = &resources;
	// ビルボードはGameViewを基準にする
	context.billboardView = (kind == RenderViewKind::Scene && gameView_.valid) ? &gameView_ : &view;

	// 中間RenderTargetをレジストリに登録してPostProcessExecutorが名前で解決できるようにする
	if (resources.GetSceneMain()) {
		registry->Register("SceneMain", resources.GetSceneMain(),
			{ "SceneColorMain", "SceneNormalMain", "ScenePositionMain",
			  "SceneMaterialMain", "SceneEmissiveMain", "SceneFlagsMain" }, std::string("SceneDepth"));
	}
	if (resources.GetSceneFinal()) {
		registry->Register("SceneFinal", resources.GetSceneFinal(), { "SceneColorFinal" }, std::nullopt);
	}

	// ビューごとのライトGPUバッファを登録
	switch (kind) {
	case RenderViewKind::Game:

		gameViewLightBuffers_.RegisterTo(context.bufferRegistry);
		gameViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	case RenderViewKind::Scene:

		if (gameView_.valid) {

			gameViewLightBuffers_.RegisterTo(context.bufferRegistry);
		} else {

			sceneViewLightBuffers_.RegisterTo(context.bufferRegistry);
		}
		sceneViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	}
	return context;
}
