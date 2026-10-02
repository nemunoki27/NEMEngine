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
#include <Engine/Core/Rendering/Renderer/Views/CameraViewProjection.h>
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

	for (const auto& viewRequest : request.views) {
		if (!viewRequest.enabled) {
			continue;
		}
		viewportRenderService_->SyncSurface(graphicsCore, viewRequest.kind, viewRequest.width, viewRequest.height);
	}
	for (const ResolvedRenderView& view : gameCameraViews_) {
		if (!view.targetTexture) {
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

				gameView.outputX = static_cast<uint32_t>(std::round(
					static_cast<float>(asset->width) * gameView.normalizedOutputX));
				gameView.outputY = static_cast<uint32_t>(std::round(
					static_cast<float>(asset->height) * gameView.normalizedOutputY));
				gameView.outputWidth = (std::max)(1u, static_cast<uint32_t>(std::round(
					static_cast<float>(asset->width) * gameView.normalizedOutputWidth)));
				gameView.outputHeight = (std::max)(1u, static_cast<uint32_t>(std::round(
					static_cast<float>(asset->height) * gameView.normalizedOutputHeight)));
				gameView.outputX = (std::min)(gameView.outputX, asset->width - 1);
				gameView.outputY = (std::min)(gameView.outputY, asset->height - 1);
				gameView.outputWidth = (std::min)(gameView.outputWidth, asset->width - gameView.outputX);
				gameView.outputHeight = (std::min)(gameView.outputHeight, asset->height - gameView.outputY);
				gameView.width = gameView.outputWidth;
				gameView.height = gameView.outputHeight;
				gameView.aspectRatio = static_cast<float>(gameView.width) / static_cast<float>(gameView.height);
				TransformComponent* transform = request.world->TryGetComponent<TransformComponent>(
					gameView.perspective.sourceCamera);
				PerspectiveCameraComponent* camera = request.world->TryGetComponent<PerspectiveCameraComponent>(
					gameView.perspective.sourceCamera);
				if (transform && camera) {
					CameraViewProjection::UpdatePerspectiveCameraMatrices(
						*transform, *camera, gameView.width, gameView.height);
					gameView.perspective = CameraViewProjection::BuildFromPerspectiveCamera(
						gameView.perspective.sourceCamera, *transform, *camera);
				}
				TransformComponent* orthographicTransform =
					request.world->TryGetComponent<TransformComponent>(
						gameView.orthographic.sourceCamera);
				OrthographicCameraComponent* orthographicCamera =
					request.world->TryGetComponent<OrthographicCameraComponent>(
						gameView.orthographic.sourceCamera);
				if (orthographicTransform && orthographicCamera) {
					CameraViewProjection::UpdateOrthographicCameraMatrices(
						*orthographicTransform, *orthographicCamera,
						gameView.width, gameView.height);
					gameView.orthographic = CameraViewProjection::BuildFromOrthographicCamera(
						gameView.orthographic.sourceCamera,
						*orthographicTransform, *orthographicCamera);
				}
				gameView.screen = CameraViewProjection::BuildScreenCamera(gameView.width, gameView.height);
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
}

SceneExecutionContext RenderPipelineRunner::BuildViewExecutionContext(GraphicsCore& graphicsCore,
	const RenderFrameRequest& request, const SceneInstance* sceneInstance,
	RenderViewKind kind, const ResolvedRenderView& view) {

	// コンテキストの構築
	SceneExecutionContext context{};
	context.kind = kind;
	context.sceneInstance = sceneInstance;
	context.view = &view;
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
		context.volumeCamera = *gameCamera;
		if (viewCamera && !graphicsCore.GetDXObject().GetFeatureController().
			ShouldUseGameViewPositionForSceneVolumes()) {
			context.volumeCamera.cameraPos = viewCamera->cameraPos;
		}
	} else if (viewCamera) {
		context.volumeCamera = *viewCamera;
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
	const ResolvedCameraView* extensionCamera = extensionView.FindCamera(RenderCameraDomain::Perspective);
	if (!extensionCamera) {
		extensionCamera = extensionView.FindCamera(RenderCameraDomain::Orthographic);
	}
	const AssetID extensionID = extensionCamera ? extensionCamera->renderExtension : AssetID{};
	const uint64_t extensionRevision = renderAssetLibrary_.GetRenderExtensionRevision();
	if (viewRenderExtension_ != extensionID ||
		viewRenderExtensionAssetRevision_ != extensionRevision) {
		const RenderExtensionAsset* extension = extensionID ?
			renderAssetLibrary_.LoadRenderExtension(extensionID) : nullptr;
		viewRenderExtensionRuntime_.Rebuild(extension ?
			ToRuntimeProfile(*extension) : RenderFeatureProfileAsset{});
		viewRenderExtension_ = extensionID;
		viewRenderExtensionAssetRevision_ = extensionRevision;
		++viewRenderExtensionGeneration_;
	}
	const ResolvedCameraView* mainGameCamera = gameViewState_.view.FindCamera(
		extensionCamera && extensionCamera->projectionMode == ResolvedProjectionMode::Orthographic ?
		RenderCameraDomain::Orthographic : RenderCameraDomain::Perspective);
	if (kind == RenderViewKind::Game && extensionCamera && mainGameCamera &&
		extensionCamera->sourceCamera == mainGameCamera->sourceCamera) {
		RenderFeatureProfileService::GetInstance().SetRuntimeExtension(
			extensionID ? renderAssetLibrary_.LoadRenderExtension(extensionID) : nullptr);
	}
	context.renderExtensionRuntime = &viewRenderExtensionRuntime_;
	context.renderExtensionGeneration = viewRenderExtensionGeneration_;
	context.drawSceneViewDefaultGrid = request.drawSceneViewDefaultGrid;
	context.drawSceneView2DCameraBounds = request.drawSceneView2DCameraBounds;
	context.allowSceneComponentOverlay = (kind == RenderViewKind::Scene);
	// 種類に応じたターゲットレジストリを選択
	RenderTargetRegistry* registry = kind == RenderViewKind::Game ?
		&gameViewState_.targetRegistry : &sceneViewState_.targetRegistry;
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
	RenderPathResources& resources = (kind == RenderViewKind::Game) ? gameViewState_.resources : sceneViewState_.resources;
	resources.Resize(graphicsCore, view.width, view.height);
	if (!resources.IsValid()) {

		Logger::Output(LogType::Engine,
			"RenderPathResourcesの作成に失敗しました view={} size={}x{}",
			EnumAdapter<RenderViewKind>::ToStringView(kind), view.width, view.height);
		context.sceneInstance = nullptr;
		return context;
	}
	context.resources = &resources;
	context.cullingResources = (context.cullingView == &gameViewState_.view) ?
		&gameViewState_.resources : &resources;
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
	if (resources.GetSceneColorOpaque()) {
		registry->Register("SceneColorOpaque", resources.GetSceneColorOpaque(),
			{ RenderTargetNames::kSceneColorOpaque }, std::nullopt);
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
	if (RenderTexture2D* texture = resources.GetSceneColorOpaque()->GetColorTexture(0)) {
		registerSceneTexture(ShaderGraphBindingNames::kSceneColor,
			texture->GetResource(), texture->GetSRVGPUHandle());
	}
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

	// ZPrepassでもRoot Signatureを満たせるよう、生成前から有効なHi-Z SRVを登録する
	if (context.cullingResources) {
		const DepthPyramidTexture& depthPyramid =
			context.cullingResources->GetDepthPyramid();
		if (depthPyramid.IsValid()) {
			RegisteredRenderBuffer entry{};
			entry.alias = DepthPyramidTexture::kBindingName;
			entry.resource = depthPyramid.GetResource();
			entry.srvGPUHandle = depthPyramid.GetSRVGPUHandle();
			entry.elementCount = depthPyramid.GetMipCount();
			context.bufferRegistry.Register(entry);
		}
	}

	// ビューごとのライトGPUバッファを登録
	switch (kind) {
	case RenderViewKind::Game:

		context.hasShadowCastingLight =
			gameViewState_.lightSet.hasShadowCastingLight;
		context.viewLights = &gameViewState_.lightSet;
		gameViewState_.lightBuffers.RegisterTo(context.bufferRegistry);
		gameViewState_.raytracingBuffers.RegisterTo(context.bufferRegistry);
		break;
	case RenderViewKind::Scene:

		context.hasShadowCastingLight =
			sceneViewState_.lightSet.hasShadowCastingLight;
		context.viewLights = &sceneViewState_.lightSet;
		sceneViewState_.lightBuffers.RegisterTo(context.bufferRegistry);
		sceneViewState_.raytracingBuffers.RegisterTo(context.bufferRegistry);
		break;
	}
	return context;
}
