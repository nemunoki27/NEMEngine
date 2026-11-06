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

bool RenderPipelineRunner::PresentTexture(GraphicsCore& graphicsCore, ID3D12GraphicsCommandList* commandList,
	D3D12_GPU_DESCRIPTOR_HANDLE source, const RenderTarget& target,
	const DisplayOutputSettings& output, float sourceWhiteNits) {

	return colorPipelineProcessor_.PresentTexture(graphicsCore, commandList, source, target,
		output, sourceWhiteNits, renderAssetLibrary_, pipelineStateCache_);
}

bool RenderPipelineRunner::PresentViewToBackBuffer(
	GraphicsCore& graphicsCore, RenderViewKind kind, AssetID material) {

	// 指定された種類の描画ビューのサーフェスを取得
	MultiRenderTarget* source = viewportRenderService_->GetSurface(kind);
	AssetDatabase* assetDatabase = renderAssetLibrary_.GetDatabase();
	if (!source || !source->GetColorTexture(0) || !assetDatabase) {
		return false;
	}
	if (!material && colorPipelineProcessor_.PresentToBackBuffer(
		graphicsCore, source, renderAssetLibrary_, pipelineStateCache_)) {
		return true;
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

	// バックバッファ全体をレンダーターゲットとしてバインド
	const RenderTarget& backBuffer = graphicsCore.GetBackBufferRenderTarget();
	dxCommand->BindRenderTargets(std::optional<RenderTarget>(backBuffer), std::nullopt);
	dxCommand->SetViewportAndScissor(backBuffer.width, backBuffer.height);

	// 全画面三角形を描画
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);

	return true;
}

void RenderPipelineRunner::SyncRequestedSurfaces(
	GraphicsCore& graphicsCore, const RenderFrameRequest& request) {

	if (viewportRenderService_->GetRenderTextureCount() != 0) {
		// 停止中や非表示のCameraも、参照中の最終出力を保持する
		const auto retainOutput = [&](const Entity&, const auto& camera) {
			const auto* surface = viewportRenderService_->GetRenderTextureSurface(camera.common.targetTexture);
			if (surface) viewportRenderService_->SyncRenderTextureSurface(
				graphicsCore, camera.common.targetTexture, surface->GetWidth(), surface->GetHeight());
		};
		request.world->ForEach<PerspectiveCameraComponent>(retainOutput);
		request.world->ForEach<OrthographicCameraComponent>(retainOutput);
	}
	viewportRenderService_->CollectRenderTextures(gameCameraViews_);
	for (const auto& viewRequest : request.views) {
		// 非表示の画面資源は再生成せず保持する
		if (!viewRequest.enabled || !viewRequest.renderThisFrame || !viewRequest.renderDefaultSurfaceThisFrame) {
			continue;
		}
		viewportRenderService_->SyncSurface(graphicsCore, viewRequest.kind, viewRequest.width, viewRequest.height);
	}
	const RenderViewRequest* gameRequest = request.FindView(RenderViewKind::Game);
	for (const ResolvedRenderView& view : gameCameraViews_) {
		if (!view.targetTexture || !gameRequest || !gameRequest->ShouldRender(view)) {
			continue;
		}
		const RenderTextureAsset* asset = renderAssetLibrary_.LoadRenderTexture(view.targetTexture);
		if (asset) {
			viewportRenderService_->SyncRenderTextureSurface(
				graphicsCore, view.targetTexture, asset->width, asset->height);
		}
	}
}

void RenderPipelineRunner::ResolveViews(const RenderFrameRequest& request) {

	if (cameraWorldLifetime_ != request.world->GetLifetime() || historySceneInstance_ != request.activeSceneInstanceID) {
		// World切替前のCamera資源を回収窓口へ渡す
		cameraStates_.clear();
		cameraWorldLifetime_ = request.world->GetLifetime();
		historySceneInstance_ = request.activeSceneInstanceID;
		++historyWorldRevision_;
	}

	gameViewState_.view = {};
	sceneViewState_.view = {};
	gameCameraViews_.clear();
	for (const auto& viewRequest : request.views) {

		ResolvedRenderView resolved = RenderViewResolver::Resolve(viewRequest, *request.world);
		switch (viewRequest.kind) {
		case RenderViewKind::Game:
			gameCameraViews_ = RenderViewResolver::ResolveGameCameraViews(viewRequest, *request.world);
			for (ResolvedRenderView& gameView : gameCameraViews_) {
				if (!gameView.targetTexture) {
					continue;
				}
				const RenderTextureAsset* asset = renderAssetLibrary_.LoadRenderTexture(gameView.targetTexture);
				if (!asset) {
					gameView.valid = false;
					continue;
				}

				RenderViewResolver::ResolveRenderTextureOutput(gameView, *request.world, asset->width, asset->height);
			}
			for (auto it = gameCameraViews_.rbegin(); it != gameCameraViews_.rend(); ++it) {
				if (it->valid && !it->targetTexture) {
					gameViewState_.view = *it;
					break;
				}
			}
			if (!gameViewState_.view.valid) {
				gameViewState_.view = resolved;
			}
			break;
		case RenderViewKind::Scene:

			sceneViewState_.view = resolved;
			break;
		}
	}
	gameViewState_.view.historyWorldRevision = historyWorldRevision_;
	sceneViewState_.view.historyWorldRevision = historyWorldRevision_;
	std::unordered_set<std::string> activeViews{};
	for (ResolvedRenderView& cameraView : gameCameraViews_) {
		cameraView.historyWorldRevision = historyWorldRevision_;
		if (cameraView.valid) activeViews.emplace(cameraView.GetHistoryKey());
	}
	if (gameViewState_.view.valid) activeViews.emplace(gameViewState_.view.GetHistoryKey());
	if (sceneViewState_.view.valid) activeViews.emplace(sceneViewState_.view.GetHistoryKey());
	// 無効化や削除されたCameraの履歴を解放する
	std::erase_if(cameraStates_, [&](const auto& state) { return !activeViews.contains(state.first); });
	colorPipelineProcessor_.RetainViews(activeViews);
	renderPath_.RetainViews(activeViews);
}

SceneExecutionContext RenderPipelineRunner::BuildViewExecutionContext(GraphicsCore& graphicsCore,
	const RenderFrameRequest& request, const SceneInstance* sceneInstance,
	RenderViewKind kind, const ResolvedRenderView& view) {

	// コンテキストの構築
	RenderPipelineViewResources& cameraState = GetCameraState(view);
	SceneExecutionContext context{};
	context.kind = kind;
	context.sceneInstance = sceneInstance;
	context.view = &view;
	if (const auto* viewRequest = request.FindView(kind); viewRequest && !view.targetTexture) {
		context.displayOutput = viewRequest->displayOutput;
	}
	context.gameView = gameViewState_.view.valid ? &gameViewState_.view : nullptr;
	// SceneViewの描画カメラは変えず、設定に応じてカリングカメラだけを切り替える
	const bool useGameViewCameraForSceneCulling = graphicsCore.GetDXObject()
		.GetFeatureController().ShouldUseGameViewCameraForSceneCulling();
	context.cullingView = (kind == RenderViewKind::Scene &&
		useGameViewCameraForSceneCulling && gameViewState_.view.valid) ?
		&gameViewState_.view : &view;
	const ResolvedCameraView* viewCamera =
		view.FindCamera(RenderCameraDomain::Perspective);
	if (!viewCamera) {
		viewCamera = view.FindCamera(RenderCameraDomain::Orthographic);
	}
	const ResolvedCameraView* gameCamera = gameViewState_.view.FindCamera(
		RenderCameraDomain::Perspective);
	if (!gameCamera) {
		gameCamera = gameViewState_.view.FindCamera(RenderCameraDomain::Orthographic);
	}
	if (kind == RenderViewKind::Scene && gameCamera) {
		// SceneViewはGame Cameraの画面設定を参照する
		context.postProcessCamera = *gameCamera;
	} else if (viewCamera) {
		context.postProcessCamera = *viewCamera;
	}
	context.defaultSurface = view.targetTexture ?
		viewportRenderService_->GetRenderTextureSurface(view.targetTexture) :
		viewportRenderService_->GetSurface(kind);
	context.useViewportRect = view.outputWidth != 0 && view.outputHeight != 0 &&
		context.defaultSurface && (view.outputX != 0 || view.outputY != 0 ||
		view.outputWidth != context.defaultSurface->GetWidth() ||
		view.outputHeight != context.defaultSurface->GetHeight());
	context.viewportX = view.outputX;
	context.viewportY = view.outputY;
	context.viewportWidth = view.outputWidth;
	context.viewportHeight = view.outputHeight;
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;
	const ResolvedRenderView& extensionView = kind == RenderViewKind::Scene && gameViewState_.view.valid ?
		gameViewState_.view : view;
	const ResolvedCameraView* extensionCamera = extensionView.FindOutputCamera();
	const AssetID extensionID = extensionCamera ? extensionCamera->renderPasses : AssetID{};
	const uint64_t extensionRevision = renderAssetLibrary_.GetRenderPassesRevision(extensionID);
	const auto overrideAsset = extensionCamera && extensionCamera->surfaceSettings.runtimeRenderPasses &&
		extensionCamera->surfaceSettings.runtimeRenderPasses->guid == extensionID &&
		extensionCamera->surfaceSettings.runtimeRenderPassesSourceRevision == extensionRevision ?
		extensionCamera->surfaceSettings.runtimeRenderPasses : nullptr;
	if (cameraState.renderPassesOverride != overrideAsset || cameraState.renderPassesAsset != extensionID ||
		cameraState.renderPassesAssetRevision != extensionRevision) {
		const RenderPassesAsset* extension = overrideAsset ? overrideAsset.get() : extensionID ?
			renderAssetLibrary_.LoadRenderPasses(extensionID) : nullptr;
		cameraState.renderPassesRuntime.Rebuild(extension ?
			ToRuntimeProfile(*extension) : RenderFeatureProfileAsset{});
		cameraState.renderPassesOverride = overrideAsset;
		cameraState.renderPassesAsset = extensionID;
		cameraState.renderPassesAssetRevision = extensionRevision;
		++cameraState.renderPassesGeneration;
	}
	const ResolvedCameraView* mainGameCamera = gameViewState_.view.FindCamera(
		extensionCamera && extensionCamera->projectionMode == ResolvedProjectionMode::Orthographic ?
		RenderCameraDomain::Orthographic : RenderCameraDomain::Perspective);
	if (kind == RenderViewKind::Game && extensionCamera && mainGameCamera &&
		extensionCamera->sourceCamera == mainGameCamera->sourceCamera) {
		RenderFeatureProfileService::GetInstance().SetRuntimeExtension(
			extensionID ? renderAssetLibrary_.LoadRenderPasses(extensionID) : nullptr, extensionRevision);
	}
	// 無効なCameraは追加Passと選択描画を使用しない
	context.renderPassesRuntime = context.postProcessCamera.valid && context.postProcessCamera.postProcessEnabled ?
		&cameraState.renderPassesRuntime : nullptr;
	context.renderPassesGeneration = cameraState.renderPassesGeneration;
	context.drawSceneViewDefaultGrid = request.drawSceneViewDefaultGrid;
	context.drawSceneView2DCameraBounds = request.drawSceneView2DCameraBounds;
	context.allowSceneComponentOverlay = (kind == RenderViewKind::Scene);
	// 種類に応じたターゲットレジストリを選択
	RenderTargetRegistry* registry = &cameraState.targetRegistry;
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
	RenderPathResources& resources = cameraState.resources;
	const auto* profile = context.renderPassesRuntime ? &context.renderPassesRuntime->GetProfile() : nullptr;
	auto& featureController = graphicsCore.GetDXObject().GetFeatureController();
	if (profile && profile->legacyFXAASettings) featureController.ImportLegacyFXAASettings(*profile->legacyFXAASettings);
	const bool boundedOutputs = !profile || std::none_of(profile->passes.begin(), profile->passes.end(), [](const auto& pass) {
		return pass.anchor == RenderFeatureAnchor::BeforeLighting;
	});
	const auto albedoFormat = colorPrecision_.Resolve(graphicsCore, renderAssetLibrary_,
		scenePreparation_.renderBatch_, request.assetDatabase, request.world, boundedOutputs);
	resources.Resize(graphicsCore, context.view->width, context.view->height, albedoFormat);
	if (!resources.IsValid()) {

		Logger::Output(LogType::Engine,
			"RenderPathResourcesの作成に失敗しました view={} size={}x{}",
			EnumAdapter<RenderViewKind>::ToStringView(kind), view.width, view.height);
		context.sceneInstance = nullptr;
		return context;
	}
	context.resources = &resources;
	context.cullingResources = (context.cullingView == &gameViewState_.view) ?
		&GetCameraState(gameViewState_.view).resources : &resources;
	context.occlusionDepthPyramidReady =
		context.cullingResources &&
		context.cullingResources->GetDepthPyramid().IsBuiltForFrame(
			GraphicsFrameState::GetFrameSerial());
	// ビルボードはGameViewを基準にする
	context.billboardView = (kind == RenderViewKind::Scene && gameViewState_.view.valid) ? &gameViewState_.view : &view;

	// 中間RenderTargetをレジストリに登録してPostProcessExecutorが名前で解決できるようにする
	if (resources.GetSceneMain()) {
		registry->Register("SceneMain", resources.GetSceneMain(),
			{ RenderTargetNames::kSceneColorMain, RenderTargetNames::kSceneNormalMain, RenderTargetNames::kScenePositionMain,
			  RenderTargetNames::kSceneMaterialMain, RenderTargetNames::kSceneEmissiveMain, RenderTargetNames::kSceneFlagsMain,
			  RenderTargetNames::kSceneMotionMain },
			std::string(RenderTargetNames::kSceneDepth));
	}
	if (resources.GetSceneFinal()) {
		registry->Register("SceneFinal", resources.GetSceneFinal(), { RenderTargetNames::kSceneColorFinal }, std::nullopt);
	}

	// Shader GraphのScene TextureをGraphics Pipelineの名前解決へ登録
	const auto registerSceneTexture = [&](const char* alias,
		ID3D12Resource* resource, D3D12_GPU_DESCRIPTOR_HANDLE handle) {

		if (!resource || handle.ptr == 0) {
			return;
		}
		context.bufferRegistry.Register(RegisteredRenderBuffer{
			.alias = alias,
			.resource = resource,
			.srvGPUHandle = handle,
		});
	};
	if (DepthTexture2D* depth = resources.GetSceneMain()->GetDepthTexture()) {
		registerSceneTexture(ShaderGraphBindingNames::kSceneDepth,
			depth->GetResource(), depth->GetSRVGPUHandle());
	}
	const auto registerGBuffer = [&](const char* alias, GBufferAttachment attachment) {

		if (RenderTexture2D* texture = resources.GetGBuffer(attachment)) {
			registerSceneTexture(alias,
				texture->GetResource(), texture->GetSRVGPUHandle());
		}
	};
	registerGBuffer(ShaderGraphBindingNames::kSceneNormal, GBufferAttachment::Normal);
	registerGBuffer(ShaderGraphBindingNames::kScenePosition, GBufferAttachment::Position);
	registerGBuffer(ShaderGraphBindingNames::kSceneMaterial, GBufferAttachment::Material);
	registerGBuffer(ShaderGraphBindingNames::kSceneEmissive, GBufferAttachment::Emissive);
	registerGBuffer(ShaderGraphBindingNames::kSceneFlags, GBufferAttachment::Flags);

	// Camera別の転送先を登録する
	context.hasShadowCastingLight = cameraState.lightSet.hasShadowCastingLight;
	context.viewLights = &cameraState.lightSet;
	cameraState.lightBuffers.RegisterTo(context.bufferRegistry);
	// 反射定数を転送してから、この描画のGPUアドレスを登録する
	const SceneSkyboxInfo skyboxInfo = SceneSkyboxResolver::Resolve(
		graphicsCore, request.assetDatabase, request.world, view.GetCullingMask(RenderCameraDomain::Perspective));
	cameraState.raytracingBuffers.Upload(*context.view, skyboxInfo, context.postProcessCamera.surfaceSettings);
	context.raytracingViewConstants = &cameraState.raytracingBuffers.GetData();
	cameraState.raytracingBuffers.RegisterTo(context.bufferRegistry);
	return context;
}
