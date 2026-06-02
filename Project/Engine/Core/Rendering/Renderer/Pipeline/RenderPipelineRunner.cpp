#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Profiling/GpuFrameProfiler.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
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

namespace {

	// スキニングする必要のあるメッシュ描画アイテムを収集して、バッチ処理する
	void PreDispatchVisibleMeshSkinning(Engine::GraphicsCore& graphicsCore,
		const Engine::SceneExecutionContext& context, const Engine::RenderSceneBatch& renderBatch,
		Engine::RenderBackendRegistry& backendRegistry, Engine::RenderAssetLibrary& assetLibrary,
		Engine::PipelineStateCache& pipelineCache, Engine::MaterialResolver& materialResolver,
		const Engine::RenderPassPhaseBuckets& passBuckets) {

		// メッシュ描画クラスを取得
		auto* baseBackend = backendRegistry.Find(Engine::RenderBackendID::Mesh);
		auto* meshBackend = dynamic_cast<Engine::MeshRenderBackend*>(baseBackend);
		if (!meshBackend || !context.view || !context.sceneInstance) {
			return;
		}

		// 描画コンテキストの構築
		Engine::RenderDrawContext drawContext{};
		drawContext.graphicsCore = &graphicsCore;
		drawContext.view = context.view;
		drawContext.cullingView = context.cullingView;
		drawContext.systemContext = context.systemContext;
		drawContext.batch = &renderBatch;
		drawContext.bufferRegistry = &context.bufferRegistry;
		drawContext.assetDatabase = context.assetDatabase;
		drawContext.assetLibrary = &assetLibrary;
		drawContext.pipelineCache = &pipelineCache;
		drawContext.materialResolver = &materialResolver;
		drawContext.forceVertexMeshVariant = context.forceVertexMeshVariant;

		// GPUのランタイム機能を取得。プレビューではRayQuery系Variantを選ばない。
		drawContext.runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
		if (context.disableInlineRayTracing) {

			drawContext.runtimeFeatures.useInlineRayTracing = false;
			drawContext.runtimeFeatures.useDispatchRays = false;
		}

		// バッチ処理のラムダ関数
		auto runDispatch = [&](const std::vector<const Engine::RenderItem*>& items) {

			size_t begin = 0;
			while (begin < items.size()) {

				const Engine::RenderItem* first = items[begin];
				if (!first || first->backendID != Engine::RenderBackendID::Mesh) {
					++begin;
					continue;
				}

				size_t end = begin + 1;
				while (end < items.size()) {
					const Engine::RenderItem* next = items[end];
					if (!next) {
						break;
					}
					if (next->backendID != first->backendID ||
						!meshBackend->CanBatch(*first, *next, drawContext.runtimeFeatures)) {
						break;
					}
					++end;
				}

				meshBackend->PreDispatchSkinningBatch(drawContext, std::span(items.data() + begin, end - begin));
				begin = end;
			}
			};

		// 全フェーズのアイテムに対してスキニングを実行する
		for (const Engine::RenderPassItemList& list : passBuckets.buckets) {
			if (!list.IsEmpty()) {
				runDispatch(list.items);
			}
		}
	}
	// ビューに対して可視なメッシュアセットIDを収集する
	void CollectVisibleMeshAssetsForView(const Engine::RenderSceneBatch& renderBatch,
		Engine::UUID sceneInstanceID, const Engine::ResolvedRenderView& view,
		std::unordered_set<Engine::AssetID>& outMeshAssets) {

		if (!view.valid) {
			return;
		}
		for (const Engine::RenderItem& item : renderBatch.GetItems()) {

			if (sceneInstanceID && item.sceneInstanceID != sceneInstanceID) {
				continue;
			}
			if (item.backendID != Engine::RenderBackendID::Mesh) {
				continue;
			}

			const Engine::ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
			if (!camera || (item.visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			const Engine::MeshRenderPayload* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(item);
			if (!payload || !payload->mesh) {
				continue;
			}
			// メッシュアセットIDを収集
			outMeshAssets.insert(payload->mesh);
		}
	}
	// 指定Entityがrootの子階層に含まれているか確認する
	bool IsEntityInPreviewTree(Engine::ECSWorld& world, Engine::Entity root, Engine::Entity entity) {

		if (!world.IsAlive(root) || !world.IsAlive(entity)) {
			return false;
		}
		if (root == entity) {
			return true;
		}

		Engine::Entity current = entity;
		while (const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(current)) {

			if (!world.IsAlive(hierarchy->parent)) {
				break;
			}
			if (hierarchy->parent == root) {
				return true;
			}
			current = hierarchy->parent;
		}
		return false;
	}
	// Entityが所属するシーンインスタンスIDを取得する
	Engine::UUID ResolveEntitySceneInstanceID(Engine::ECSWorld& world, Engine::Entity entity, Engine::UUID fallback) {

		Engine::UUID id = Engine::SceneObjectUtility::GetSceneInstanceID(world, entity);
		return id ? id : fallback;
	}
	// プレビュー対象の描画アイテムだけを描画フェーズごとに振り分ける
	void BuildPreviewPassBuckets(Engine::ECSWorld& world, Engine::Entity root,
		const Engine::RenderSceneBatch& renderBatch, const Engine::ResolvedRenderView& view,
		Engine::RenderPassPhaseBuckets& outBuckets, std::vector<Engine::AssetID>& outMeshAssets) {

		outBuckets.Clear();
		outMeshAssets.clear();

		std::unordered_set<uint64_t> meshAssetSet{};
		for (const Engine::RenderItem& item : renderBatch.GetItems()) {

			if (!IsEntityInPreviewTree(world, root, item.entity)) {
				continue;
			}

			const Engine::ResolvedCameraView* camera = view.FindCamera(item.cameraDomain);
			if (!camera || (item.visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			outBuckets.Get(item.renderPhase).items.emplace_back(&item);
			if (item.backendID == Engine::RenderBackendID::Mesh) {

				if (const auto* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(item)) {
					if (payload->mesh) {
						meshAssetSet.emplace(payload->mesh.value);
					}
				}
			}
		}

		outMeshAssets.reserve(meshAssetSet.size());
		for (uint64_t meshValue : meshAssetSet) {
			outMeshAssets.emplace_back(Engine::AssetID{ meshValue });
		}
	}
}

void Engine::RenderPipelineRunner::Init() {

	viewportRenderService_ = std::make_unique<ViewportRenderService>();

	// 描画アイテム抽出器の登録
	extractorRegistry_.Clear();
	extractorRegistry_.Register(std::make_unique<SpriteRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<TextRenderItemExtractor>());
	extractorRegistry_.Register(std::make_unique<MeshRenderItemExtractor>());
	// 描画バックエンドの登録
	backendRegistry_.Clear();
	backendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	backendRegistry_.Register(std::make_unique<TextRenderBackend>());
	backendRegistry_.Register(std::make_unique<MeshRenderBackend>());
	// ツールプレビューはメインビューとは別のGPUバッファを持たせる
	previewBackendRegistry_.Clear();
	previewBackendRegistry_.Register(std::make_unique<SpriteRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<TextRenderBackend>());
	previewBackendRegistry_.Register(std::make_unique<MeshRenderBackend>());
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
	gameViewLightCullingBuffers_.Release();
	sceneViewLightCullingBuffers_.Release();
	previewLightBufferPool_.Clear();
	previewLightCullingBufferPool_.Clear();
	previewBackendFrameStarted_ = false;
	lastRenderedWorld_ = nullptr;
}

void Engine::RenderPipelineRunner::Finalize() {

	// GPU計測用のクエリヒープ/リードバックバッファはここで解放する。
	// シングルトンのため放置するとDeviceより後まで生き残り、LeakCheckerに残る。
	GpuFrameProfiler::GetInstance().Finalize();

	renderPath_.Finalize();
	backendRegistry_.Clear();
	previewBackendRegistry_.Clear();
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
	gameViewLightCullingBuffers_.Release();
	sceneViewLightCullingBuffers_.Release();
	previewLightBufferPool_.Clear();
	previewLightCullingBufferPool_.Clear();
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

void Engine::RenderPipelineRunner::Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	// エディタPostSceneで複数のプレビューを描画するため、プレビューbackendのリソースプールは
	// ここでフレーム境界だけリセットし、RenderEntityPreviewごとにはリセットしない。
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
		auto clearMeshCache = [](RenderBackendRegistry& registry) {
			auto* base = registry.Find(RenderBackendID::Mesh);
			if (auto* mesh = dynamic_cast<MeshRenderBackend*>(base)) {
				mesh->ClearStaticBatchCache();
			}
		};
		clearMeshCache(backendRegistry_);
		clearMeshCache(previewBackendRegistry_);
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
	GpuFrameProfiler::GetInstance().BeginFrame(graphicsCore.GetDXObject().GetDevice(),
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

	// メッシュ描画クラスの取得
	auto* meshBackendBase = backendRegistry_.Find(RenderBackendID::Mesh);
	auto* meshBackend = dynamic_cast<MeshRenderBackend*>(meshBackendBase);

	std::unordered_set<AssetID> visibleMeshSet{};
	visibleMeshSet.reserve(renderBatch_.GetItems().size());

	// ビューごとに可視なメッシュアセットIDを収集
	if (meshBackend && activeScene) {
		if (gameView_.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, gameView_, visibleMeshSet);
		}
		if (sceneView_.valid) {
			CollectVisibleMeshAssetsForView(renderBatch_, activeScene->instanceID, sceneView_, visibleMeshSet);
		}
	}

	std::vector<AssetID> visibleMeshes{};
	visibleMeshes.reserve(visibleMeshSet.size());
	for (const AssetID& id : visibleMeshSet) {
		visibleMeshes.emplace_back(id);
	}
	// GPUに可視なメッシュの情報を要求して、必要なリソースを準備
	if (meshBackend && !visibleMeshes.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, visibleMeshes);
	}

	// ビューごとのライト集合クリア
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	const bool sceneViewUsesGameLightCulling = sceneView_.valid && gameView_.valid;
	// ルートシーン用のビューライト構築
	if (activeScene) {
		if (gameView_.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, gameView_, gameViewLightSet_);
		}
		if (sceneView_.valid && !sceneViewUsesGameLightCulling) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, sceneView_, sceneViewLightSet_);
		}
	}

	// GPUライトバッファ初期化
	if (!gameViewLightBuffers_.IsInitialized()) {
		gameViewLightBuffers_.Init(graphicsCore);
	}
	if (!sceneViewUsesGameLightCulling && !sceneViewLightBuffers_.IsInitialized()) {
		sceneViewLightBuffers_.Init(graphicsCore);
	}
	if (!gameViewLightCullingBuffers_.IsInitialized()) {
		gameViewLightCullingBuffers_.Init(graphicsCore);
	}
	if (!sceneViewUsesGameLightCulling && !sceneViewLightCullingBuffers_.IsInitialized()) {
		sceneViewLightCullingBuffers_.Init(graphicsCore);
	}
	const auto& runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	const uint32_t lightCullingMode = static_cast<uint32_t>(runtimeFeatures.lightCullingMode);
	// ビューごとのライト集合をGPUへ転送
	gameViewLightBuffers_.Upload(gameViewLightSet_);
	// ビューごとのライトカリングデータをGPUへ転送
	gameViewLightCullingBuffers_.Upload(gameView_, gameViewLightSet_, lightCullingMode);
	if (!sceneViewUsesGameLightCulling) {

		sceneViewLightBuffers_.Upload(sceneViewLightSet_);
		sceneViewLightCullingBuffers_.Upload(sceneView_, sceneViewLightSet_, lightCullingMode);
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
		RenderPassPhaseBuckets passBuckets{};
		RenderPassItemCollector::BuildBucketsForViewAndScene(
			renderBatch_, view, context.sceneInstance->instanceID, passBuckets);

		// スキニングメッシュの頂点更新
		if (meshBackend) {
			PreDispatchVisibleMeshSkinning(graphicsCore, context,
				renderBatch_, backendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets);

			// レイトレーシングシーンの構築。
			// gRaytracingSceneInstances/gRaytracingSubMeshes は context.bufferRegistry へ登録する必要があるため、
			// コピーではなく実際の context へ直接構築する。コピーへ構築すると登録が破棄され、反射パスが早期リターンする。
			// TLAS構築の基準ビューだけ一時的にGameViewへ差し替え、構築後に元へ戻す。
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
		renderPath_.Execute(graphicsCore, passBuckets, context);

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
	GpuFrameProfiler::GetInstance().Resolve(graphicsCore.GetDXObject().GetDxCommand()->GetCommandList());
}

bool Engine::RenderPipelineRunner::PresentViewToBackBuffer(
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
	const MaterialPassBinding* passBinding = FindPass(*materialAsset, "Blit");
	if (!passBinding) {
		passBinding = FindPass(*materialAsset, "Fullscreen");
	}
	// 無効なパスは処理しない
	if (passBinding->preferredVariant == PipelineVariantKind::Compute ||
		passBinding->preferredVariant == PipelineVariantKind::Raytracing) {
		return false;
	}

	// バックバッファのフォーマットに合わせたパイプラインステートを取得
	std::vector<DXGI_FORMAT> rtvFormats = {
		graphicsCore.GetBackBufferRenderTarget().format
	};
	const PipelineState* pipelineState = pipelineStateCache_.GetORCreate(graphicsCore.GetDXObject(),
		renderAssetLibrary_, passBinding->pipeline, passBinding->preferredVariant, rtvFormats, DXGI_FORMAT_UNKNOWN);

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

bool Engine::RenderPipelineRunner::RenderEntityPreview(
	GraphicsCore& graphicsCore, const EntityPreviewRenderRequest& request) {

	if (!request.world || !request.assetDatabase || !request.surface || !request.surface->IsValid() ||
		!request.world->IsAlive(request.rootEntity)) {
		return false;
	}

	// メインのScene/Gameとは別に、ツール用サーフェイスだけを描画対象にする。
	renderAssetLibrary_.Init(request.assetDatabase);
	if (!previewBackendFrameStarted_) {

		previewBackendRegistry_.BeginFrame(graphicsCore);
		previewLightBufferPool_.BeginFrame();
		previewLightCullingBufferPool_.BeginFrame();
		previewBackendFrameStarted_ = true;
	}
	extractorRegistry_.BuildBatch(*request.world, renderBatch_);
	lightExtractorRegistry_.BuildBatch(*request.world, frameLightBatch_);

	RenderViewRequest viewRequest{};
	viewRequest.kind = RenderViewKind::Scene;
	viewRequest.enabled = true;
	viewRequest.width = request.useViewportRect && request.viewportWidth > 0 ? request.viewportWidth : request.surface->GetWidth();
	viewRequest.height = request.useViewportRect && request.viewportHeight > 0 ? request.viewportHeight : request.surface->GetHeight();
	viewRequest.sourceKind = RenderViewSourceKind::ManualCamera;
	viewRequest.manualCamera = request.camera;
	ResolvedRenderView previewView = RenderViewResolver::Resolve(viewRequest, *request.world);
	if (!previewView.valid) {
		return false;
	}

	SceneInstance previewScene{};
	previewScene.instanceID = ResolveEntitySceneInstanceID(*request.world, request.rootEntity, request.sceneInstanceID);
	if (request.sceneHeader) {
		previewScene.header = *request.sceneHeader;
		previewScene.header.subScenes.clear();
	}

	RenderPassPhaseBuckets passBuckets{};
	std::vector<AssetID> meshAssets{};
	BuildPreviewPassBuckets(*request.world, request.rootEntity, renderBatch_, previewView, passBuckets, meshAssets);

	RenderTargetRegistry previewTargetRegistry{};
	previewTargetRegistry.BeginFrame();
	previewTargetRegistry.Register("View", request.surface, { "Preview.Color" }, std::string("Preview.Depth"));
	previewTargetRegistry.Register(ViewportRenderService::GetViewAlias(RenderViewKind::Scene),
		request.surface, { "Preview.Color" }, std::string("Preview.Depth"));

	SceneExecutionContext context{};
	context.kind = RenderViewKind::Scene;
	context.sceneInstance = &previewScene;
	context.view = &previewView;
	context.defaultSurface = request.surface;
	context.targetRegistry = &previewTargetRegistry;
	context.useViewportRect = request.useViewportRect;
	context.viewportX = request.viewportX;
	context.viewportY = request.viewportY;
	context.viewportWidth = request.viewportWidth;
	context.viewportHeight = request.viewportHeight;
	context.disableInlineRayTracing = true;
	context.forceVertexMeshVariant = request.forceVertexMeshVariant;
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;

	// プレビュー用のライトバッファを更新する。SceneView/GameViewのGPUバッファは触らない。
	previewLightSet_.Clear();
	ViewLightCollector::CollectForView(frameLightBatch_, &previewScene, previewView, previewLightSet_);
	ViewLightBufferSet& previewLightBuffers = previewLightBufferPool_.Acquire(graphicsCore,
		[](ViewLightBufferSet& buffers, GraphicsCore& core) {
			buffers.Init(core);
		});
	ViewLightCullingBufferSet& previewLightCullingBuffers = previewLightCullingBufferPool_.Acquire(graphicsCore,
		[](ViewLightCullingBufferSet& buffers, GraphicsCore& core) {
			buffers.Init(core);
	});
	previewLightBuffers.Upload(previewLightSet_);
	const auto& runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();
	previewLightCullingBuffers.Upload(previewView, previewLightSet_,
		static_cast<uint32_t>(runtimeFeatures.lightCullingMode));
	previewLightBuffers.RegisterTo(context.bufferRegistry);
	previewLightCullingBuffers.RegisterTo(context.bufferRegistry);

	auto* meshBackendBase = previewBackendRegistry_.Find(RenderBackendID::Mesh);
	auto* meshBackend = dynamic_cast<MeshRenderBackend*>(meshBackendBase);
	if (meshBackend && !meshAssets.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, meshAssets);
		PreDispatchVisibleMeshSkinning(graphicsCore, context,
			renderBatch_, previewBackendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets);
	}

	// プレビュー: クリア → 全フェーズを描画サーフェスへ直接描画
	auto* dxCommand = graphicsCore.GetDXObject().GetDxCommand();
	if (request.clearSurface) {

		MultiRenderTargetClearDesc clearDesc{};
		clearDesc.clearColor = true;
		clearDesc.clearColorValue = request.clearColor;
		clearDesc.clearDepth = (request.surface->GetDepthTexture() != nullptr);
		clearDesc.clearDepthValue = 1.0f;
		clearDesc.clearStencil = false;

		request.surface->TransitionForRender(*dxCommand);
		request.surface->Bind(*dxCommand);
		if (request.useViewportRect && request.viewportWidth > 0 && request.viewportHeight > 0) {
			dxCommand->SetViewportAndScissor(request.viewportX, request.viewportY,
				request.viewportWidth, request.viewportHeight);
		} else {
			dxCommand->SetViewportAndScissor(request.surface->GetWidth(), request.surface->GetHeight());
		}
		request.surface->Clear(*dxCommand, clearDesc);
	}

	for (const Engine::RenderPassItemList& list : passBuckets.buckets) {
		if (list.IsEmpty()) {
			continue;
		}
		request.surface->TransitionForRender(*dxCommand);
		request.surface->Bind(*dxCommand);
		if (request.useViewportRect && request.viewportWidth > 0 && request.viewportHeight > 0) {
			dxCommand->SetViewportAndScissor(request.viewportX, request.viewportY,
				request.viewportWidth, request.viewportHeight);
		} else {
			dxCommand->SetViewportAndScissor(request.surface->GetWidth(), request.surface->GetHeight());
		}
		batchDispatcher_.Dispatch(graphicsCore, context, renderBatch_, previewBackendRegistry_,
			renderAssetLibrary_, pipelineStateCache_, materialResolver_,
			list.items, request.surface, nullptr, "Draw", false);
	}

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	if (request.drawGrid2D) {

		LineRenderer::GetInstance()->Get2D()->DrawGrid();
	}
	if (request.drawGrid3D) {

		LineRenderer::GetInstance()->Get3D()->DrawGrid();
	}
	LineRenderer::GetInstance()->RenderSceneView(graphicsCore, previewView, *request.surface, false, false);
#endif

	request.surface->TransitionForShaderRead(*dxCommand);

	return true;
}

void Engine::RenderPipelineRunner::SyncRequestedSurfaces(
	GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	for (const auto& viewRequest : request.views) {
		if (!viewRequest.enabled) {
			continue;
		}
		viewportRenderService_->SyncSurface(graphicsCore, viewRequest.kind, viewRequest.width, viewRequest.height);
	}
}

void Engine::RenderPipelineRunner::ResolveViews(const RenderFrameRequest& request) {

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

Engine::SceneExecutionContext Engine::RenderPipelineRunner::BuildViewExecutionContext(GraphicsCore& graphicsCore,
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
	// SceneViewは描画カメラだけSceneViewにして、カリング基準はGameViewに揃える
	context.lightCullingResources = (kind == RenderViewKind::Scene && gameView_.valid) ?
		&gameViewResources_ : &resources;

	// 中間RenderTargetをレジストリに登録してPostProcessExecutorが名前で解決できるようにする
	if (resources.GetSceneMain()) {
		registry->Register("SceneMain", resources.GetSceneMain(),
			{ "SceneColorMain", "SceneNormalMain", "ScenePositionMain" }, std::string("SceneDepth"));
	}
	if (resources.GetSceneFinal()) {
		registry->Register("SceneFinal", resources.GetSceneFinal(), { "SceneColorFinal" }, std::nullopt);
	}

	// ビューごとのライトGPUバッファを登録
	switch (kind) {
	case RenderViewKind::Game:

		gameViewLightBuffers_.RegisterTo(context.bufferRegistry);
		gameViewLightCullingBuffers_.RegisterTo(context.bufferRegistry);
		gameViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	case RenderViewKind::Scene:

		if (gameView_.valid) {

			gameViewLightBuffers_.RegisterTo(context.bufferRegistry);
			gameViewLightCullingBuffers_.RegisterTo(context.bufferRegistry);
		} else {

			sceneViewLightBuffers_.RegisterTo(context.bufferRegistry);
			sceneViewLightCullingBuffers_.RegisterTo(context.bufferRegistry);
		}
		sceneViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	}
	return context;
}
