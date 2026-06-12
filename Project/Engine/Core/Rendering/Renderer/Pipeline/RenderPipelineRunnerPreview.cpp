#include "RenderPipelineRunner.h"
#include "RenderPipelineUtility.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/ViewLightCollector.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandContext.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

//============================================================================
//	RenderPipelineRunner classMethods (Preview)
//============================================================================

bool RenderPipelineRunner::RenderEntityPreview(
	GraphicsCore& graphicsCore, const EntityPreviewRenderRequest& request) {

	if (!request.world || !request.assetDatabase || !request.surface || !request.surface->IsValid() ||
		!request.world->IsAlive(request.rootEntity)) {
		return false;
	}

	// メインのScene/Gameとは別に、ツール用サーフェイスだけを描画対象にする
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
	context.forceDirectLocalLightEvaluation = true;
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;

	// プレビュー用のライトバッファを更新しSceneView/GameViewのGPUバッファは触らない
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
	previewLightCullingBuffers.Upload(previewView, previewLightSet_, LightCullingMode::Disabled);
	previewLightBuffers.RegisterTo(context.bufferRegistry);
	previewLightCullingBuffers.RegisterTo(context.bufferRegistry);

	auto* meshBackendBase = previewBackendRegistry_.Find(RenderBackendID::Mesh);
	auto* meshBackend = dynamic_cast<MeshRenderBackend*>(meshBackendBase);
	if (meshBackend && !meshAssets.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, meshAssets);
		PreDispatchVisibleMeshSkinning(graphicsCore, context,
			renderBatch_, previewBackendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets);
	}

	// プレビュー:クリア→全フェーズを描画サーフェスへ直接描画
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

	for (const RenderPassItemList& list : passBuckets.buckets) {
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
			list.items, request.surface, nullptr, MaterialPassKind::Draw, false);
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
