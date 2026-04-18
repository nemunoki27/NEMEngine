#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/View/RenderViewResolver.h>
#include <Engine/Core/Graphics/Render/Texture/MultiRenderTarget.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Sprite/SpriteRenderItemExtractor.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Text/TextRenderItemExtractor.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderItemExtractor.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Sprite/SpriteRenderBackend.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Text/TextRenderBackend.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Graphics/Render/Light/Builtin/Directional/DirectionalLightExtractor.h>
#include <Engine/Core/Graphics/Render/Light/Builtin/Point/PointLightExtractor.h>
#include <Engine/Core/Graphics/Render/Light/Builtin/Spot/SpotLightExtractor.h>
#include <Engine/Core/Graphics/Render/Light/ViewLightCollector.h>
#include <Engine/Core/Graphics/Render/Queue/RenderPassItemCollector.h>
#include <Engine/Core/Graphics/Render/Pass/ScenePassExecutor.h>
#include <Engine/Core/Graphics/Line/LineRenderer.h>
#include <Engine/Core/Scene/Instance/SceneInstanceManager.h>
#include <Engine/Core/Graphics/DxObject/DxCommand.h>
#include <Engine/Core/Graphics/Asset/MaterialAsset.h>

//============================================================================
//	RenderPipelineRunner classMethods
//============================================================================

namespace {

	// 深度前描画でスキニングする必要のあるメッシュ描画アイテムを収集する
	std::vector<const Engine::RenderItem*> CollectDepthPrepassMeshItemsForSkinning(const Engine::RenderSceneBatch& renderBatch,
		const Engine::SceneExecutionContext& context, const Engine::DepthPrepassPassDesc& pass,
		const Engine::RenderPassPhaseBuckets& passBuckets) {

		std::vector<const Engine::RenderItem*> result{};
		// 深度前描画のキューに属するアイテムを収集
		const Engine::RenderPassItemList* list = passBuckets.Find(pass.queue);
		if (!list || list->IsEmpty()) {
			return result;
		}
		// カメラを取得
		const Engine::ResolvedCameraView* camera = context.view->FindCamera(Engine::RenderCameraDomain::Perspective);
		if (!camera) {
			return result;
		}

		result.reserve(list->items.size());
		for (const Engine::RenderItem* item : list->items) {

			if (!item) {
				continue;
			}
			if (item->backendID != Engine::RenderBackendID::Mesh) {
				continue;
			}
			if ((item->visibilityLayerMask & camera->cullingMask) == 0) {
				continue;
			}

			const Engine::MeshRenderPayload* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(*item);
			// 深度描画対象のみ
			if (!payload || !payload->enableZPrepass) {
				continue;
			}
			result.emplace_back(item);
		}
		return result;
	}
	// スキニングする必要のあるメッシュ描画アイテムを収集して、バッチ処理する
	void PreDispatchVisibleMeshSkinning(Engine::GraphicsCore& graphicsCore, const Engine::SceneHeader& header,
		const Engine::SceneExecutionContext& context, const Engine::RenderSceneBatch& renderBatch,
		Engine::RenderBackendRegistry& backendRegistry, Engine::RenderAssetLibrary& assetLibrary,
		Engine::PipelineStateCache& pipelineCache, Engine::MaterialResolver& materialResolver) {

		// メッシュ描画クラスを取得
		auto* baseBackend = backendRegistry.Find(Engine::RenderBackendID::Mesh);
		auto* meshBackend = dynamic_cast<Engine::MeshRenderBackend*>(baseBackend);
		if (!meshBackend || !context.view || !context.sceneInstance) {
			return;
		}

		Engine::RenderPassPhaseBuckets passBuckets{};
		// ビューとシーンに対して、描画パスのバケットを構築する
		Engine::RenderPassItemCollector::BuildBucketsForViewAndScene(
			renderBatch, *context.view, context.sceneInstance->instanceID, passBuckets);

		// 描画コンテキストの構築
		Engine::RenderDrawContext drawContext{};
		drawContext.graphicsCore = &graphicsCore;
		drawContext.view = context.view;
		drawContext.systemContext = context.systemContext;
		drawContext.batch = &renderBatch;
		drawContext.bufferRegistry = &context.bufferRegistry;
		drawContext.assetDatabase = context.assetDatabase;
		drawContext.assetLibrary = &assetLibrary;
		drawContext.pipelineCache = &pipelineCache;
		drawContext.materialResolver = &materialResolver;

		// GPUのランタイムの機能情報を取得
		const auto& runtimeFeatures = graphicsCore.GetDXObject().GetFeatureController().GetRuntimeFeatures();

		// バッチ処理のラムダ関数
		auto runDispatch = [&](const std::vector<const Engine::RenderItem*>& items) {

			size_t begin = 0;
			// アイテムをすべて処理するまでループ
			while (begin < items.size()) {

				const Engine::RenderItem* first = items[begin];
				// アイテムが有効で、メッシュ描画アイテムで、バッチ処理可能なものであるか
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
					// バッチ処理可能かどうかを判定
					if (next->backendID != first->backendID || !meshBackend->CanBatch(*first, *next, runtimeFeatures)) {
						break;
					}
					++end;
				}

				// スキニング実行
				meshBackend->PreDispatchSkinningBatch(drawContext, std::span(items.data() + begin, end - begin));

				// 次のバッチ処理の開始位置を更新
				begin = end;
			}
			};

		for (const auto& pass : header.passOrder) {
			switch (pass.type) {
			case Engine::ScenePassType::DepthPrepass: {

				runDispatch(CollectDepthPrepassMeshItemsForSkinning(renderBatch, context, pass.depthPrepass, passBuckets));
				break;
			}
			case Engine::ScenePassType::Draw: {

				const Engine::RenderPassItemList* list = passBuckets.Find(pass.draw.queue);
				if (list && !list->IsEmpty()) {

					runDispatch(list->items);
				}
				break;
			}
			}
		}
	}
	// ビューに対して可視なメッシュアセットIDを収集する
	void CollectVisibleMeshAssetsForView(const Engine::RenderSceneBatch& renderBatch,
		Engine::UUID sceneInstanceID, const Engine::ResolvedRenderView& view,
		std::unordered_set<Engine::AssetID>& outMeshAssets) {

		Engine::RenderPassPhaseBuckets passBuckets{};
		Engine::RenderPassItemCollector::BuildBucketsForViewAndScene(renderBatch, view, sceneInstanceID, passBuckets);
		for (const auto& [phase, itemList] : passBuckets.phaseToItems) {
			for (const Engine::RenderItem* item : itemList.items) {

				if (!item || item->backendID != Engine::RenderBackendID::Mesh) {
					continue;
				}

				const Engine::MeshRenderPayload* payload = renderBatch.GetPayload<Engine::MeshRenderPayload>(*item);

				if (!payload || !payload->mesh) {
					continue;
				}
				// メッシュアセットIDを収集
				outMeshAssets.insert(payload->mesh);
			}
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
	// ライト抽出器の登録
	lightExtractorRegistry_.Clear();
	lightExtractorRegistry_.Register(std::make_unique<DirectionalLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<PointLightExtractor>());
	lightExtractorRegistry_.Register(std::make_unique<SpotLightExtractor>());

	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	frameLightBatch_.Clear();
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();

	raytracingPipelineStateCache_.Clear();
	gameViewRaytracingBuffers_.Release();
	sceneViewRaytracingBuffers_.Release();

	// ビューライトバッファの初期化
	gameViewLightBuffers_.Release();
	sceneViewLightBuffers_.Release();
	gameViewLightCullingBuffers_.Release();
	sceneViewLightCullingBuffers_.Release();
}

void Engine::RenderPipelineRunner::Finalize() {

	backendRegistry_.Clear();
	extractorRegistry_.Clear();
	renderAssetLibrary_.Clear();
	pipelineStateCache_.Clear();
	materialResolver_.Clear();
	lightExtractorRegistry_.Clear();
	frameLightBatch_.Clear();
	gameViewLightSet_.Clear();
	sceneViewLightSet_.Clear();
	gameViewLightBuffers_.Release();
	sceneViewLightBuffers_.Release();
	gameViewLightCullingBuffers_.Release();
	sceneViewLightCullingBuffers_.Release();
	viewportRenderService_.reset();
	raytracingPipelineStateCache_.Clear();
	gameViewRaytracingBuffers_.Release();
	sceneViewRaytracingBuffers_.Release();
	raytracingSceneBuilder_.Finalize();
}

void Engine::RenderPipelineRunner::Render(GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	// ワールドがない場合は描画できないので処理しない
	if (!request.world) {
		return;
	}

	// データクリア
	sceneViewTLASResource_ = nullptr;
	sceneViewPickRecords_.clear();

	// アセットライブラリの初期化、フレーム開始処理
	renderAssetLibrary_.Init(request.assetDatabase);
	backendRegistry_.BeginFrame(graphicsCore);

	// レイトレシーンフレーム開始処理
	raytracingSceneBuilder_.BeginFrame(graphicsCore);

	// 描画要求に基づいて必要なサーフェイスをGPUと同期し、ビュー情報を決定
	SyncRequestedSurfaces(graphicsCore, request);
	ResolveViews(request);

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
	// ルートシーン用のビューライト構築
	if (activeScene) {
		if (gameView_.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, gameView_, gameViewLightSet_);
		}
		if (sceneView_.valid) {

			ViewLightCollector::CollectForView(frameLightBatch_, activeScene, sceneView_, sceneViewLightSet_);
		}
	}

	// GPUライトバッファ初期化
	if (!gameViewLightBuffers_.IsInitialized()) {
		gameViewLightBuffers_.Init(graphicsCore);
	}
	if (!sceneViewLightBuffers_.IsInitialized()) {
		sceneViewLightBuffers_.Init(graphicsCore);
	}
	if (!gameViewLightCullingBuffers_.IsInitialized()) {
		gameViewLightCullingBuffers_.Init(graphicsCore);
	}
	if (!sceneViewLightCullingBuffers_.IsInitialized()) {
		sceneViewLightCullingBuffers_.Init(graphicsCore);
	}
	// ビューごとのライト集合をGPUへ転送
	gameViewLightBuffers_.Upload(gameViewLightSet_);
	sceneViewLightBuffers_.Upload(sceneViewLightSet_);
	// ビューごとのライトカリングデータをGPUへ転送
	gameViewLightCullingBuffers_.Upload(gameView_, gameViewLightSet_);
	sceneViewLightCullingBuffers_.Upload(sceneView_, sceneViewLightSet_);

	// レイトレーシングビュー関連バッファの初期化と転送
	if (!gameViewRaytracingBuffers_.IsInitialized()) {
		gameViewRaytracingBuffers_.Init(graphicsCore);
	}
	if (!sceneViewRaytracingBuffers_.IsInitialized()) {
		sceneViewRaytracingBuffers_.Init(graphicsCore);
	}
	gameViewRaytracingBuffers_.Upload(gameView_);
	sceneViewRaytracingBuffers_.Upload(sceneView_);

	// シーン描画の実行
	ScenePassExecutor::Dependencies deps{};
	deps.renderBatch = &renderBatch_;
	deps.backendRegistry = &backendRegistry_;
	deps.assetLibrary = &renderAssetLibrary_;
	deps.pipelineCache = &pipelineStateCache_;
	deps.materialResolver = &materialResolver_;
	deps.viewportService = viewportRenderService_.get();
	deps.dispatcher = &batchDispatcher_;
	deps.raytracingPipelineCache = &raytracingPipelineStateCache_;
	ScenePassExecutor executor{ deps };

	// 描画ビューごとに描画を実行
	auto renderView = [&](RenderViewKind kind, const ResolvedRenderView& view) {
		if (!view.valid) {
			return;
		}

		SceneExecutionContext context = BuildViewExecutionContext(graphicsCore, request, activeScene, kind, view);
		if (!context.sceneInstance) {
			return;
		}

		// スキニングメッシュの頂点更新
		if (meshBackend) {
			PreDispatchVisibleMeshSkinning(graphicsCore, context.sceneInstance->header, context,
				renderBatch_, backendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_);

			// レイトレーシングシーンの構築
			raytracingSceneBuilder_.BuildForScene(graphicsCore, *request.assetDatabase, meshBackend, renderBatch_, context);
		}

		// TLASリソースとピック用のサブメッシュ情報を取得して保存
		if (kind == RenderViewKind::Scene) {

			sceneViewTLASResource_ = context.raytracing.tlasResource;
			sceneViewPickRecords_ = raytracingSceneBuilder_.GetPickRecords();
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

		// シーンの実行
		executor.ExecuteScene(graphicsCore, request, context);

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		if (kind == RenderViewKind::Scene && context.defaultSurface) {

			LineRenderer::GetInstance()->RenderSceneView(graphicsCore, view, *context.defaultSurface);
		}
#endif

		executor.TransitionAllTargetsToShaderRead(graphicsCore, context);
		};
	renderView(RenderViewKind::Game, gameView_);
	renderView(RenderViewKind::Scene, sceneView_);
}

bool Engine::RenderPipelineRunner::PresentViewToBackBuffer(
	GraphicsCore& graphicsCore, RenderViewKind kind, AssetID material) {

	// 指定された種類の描画ビューのサーフェスを取得
	MultiRenderTarget* source = viewportRenderService_->GetSurface(kind);
	AssetDatabase* assetDatabase = renderAssetLibrary_.GetDatabase();

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
		graphicsCore.GetSwapChainDesc().Format
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
	context.defaultSurface = viewportRenderService_->GetSurface(kind);
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;
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
	const SceneHeader* header = sceneInstance ? &sceneInstance->header : request.header;
	if (header) {
		for (const auto& renderTarget : header->renderTargets) {

			registry->ResizeTransient(graphicsCore, renderTarget, view.width, view.height);
		}
	}
	// ビューごとのライトGPUバッファを登録
	switch (kind) {
	case RenderViewKind::Game:

		gameViewLightBuffers_.RegisterTo(context.bufferRegistry);
		gameViewLightCullingBuffers_.RegisterTo(context.bufferRegistry);
		gameViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	case RenderViewKind::Scene:

		sceneViewLightBuffers_.RegisterTo(context.bufferRegistry);
		sceneViewLightCullingBuffers_.RegisterTo(context.bufferRegistry);
		sceneViewRaytracingBuffers_.RegisterTo(context.bufferRegistry);
		break;
	}
	return context;
}