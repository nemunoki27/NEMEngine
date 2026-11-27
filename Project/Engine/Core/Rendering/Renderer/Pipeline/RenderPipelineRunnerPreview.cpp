#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include "RenderPipelineUtility.h"
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Primitive/PrimitiveRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/SceneSkyboxResolver.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/Passes/RenderFeatureResourceUtility.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/RenderTargetNames.h>
#include <Engine/Core/Rendering/PostProcess/PostProcessBindingNames.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphBindingNames.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewResolver.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Lighting/ViewLightCollector.h>
#include <Engine/Core/Rendering/Renderer/Queues/RenderPassItemCollector.h>
#include <Engine/Core/Rendering/Renderer/Passes/RenderItemBatchDispatcher.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
#include <Engine/Core/Rendering/DebugDraw/Lines/LineRenderer.h>
#endif

using namespace Engine;

const MeshGPUResource* RenderPipelineRunner::FindPreviewMesh(AssetID asset) const {

	// 非同期読込の公開済み結果だけを参照する
	return previewResources_.previewMeshBackend_ ? previewResources_.previewMeshBackend_->FindMeshResource(asset) : nullptr;
}

//============================================================================
//	RenderPipelineRunner classMethods (Preview)
//============================================================================

uint64_t RenderPipelineRunner::PreparePreviewMeshes(GraphicsCore& graphicsCore, AssetDatabase& database,
	std::span<const AssetID> assets) {

	if (!previewResources_.previewMeshBackend_) {
		return 0;
	}
	previewResources_.BeginFrame(graphicsCore);
	// 再描画しないframeも非同期読込を完了させる
	previewResources_.previewMeshBackend_->RequestMeshes(graphicsCore, database, assets);
	return previewResources_.previewMeshBackend_->GetMeshResourceRevision();
}

RaytracingPipelineReadiness RenderPipelineRunner::PrepareRaytracingPreview(GraphicsCore& graphicsCore, const MaterialAsset& material) {

	const auto* pass = FindPass(material, MaterialPassKind::RayTracing);
	if (!pass) return RaytracingPipelineReadiness::Failed;
	PipelineStaticSamplerOverrideSet samplers{};
	samplers.fillMissingSamplers = true;
	return raytracingPipelineStateCache_.PrepareLatest(graphicsCore.GetDXObject(), renderAssetLibrary_, pass->pipeline, pass->shaderOverride, samplers);
}

bool RenderPipelineRunner::DrawPreviewEffect(GraphicsCore& graphicsCore, SceneExecutionContext& context, AssetID materialID) {

	auto* output = context.defaultSurface->GetColorTexture(0);
	const MaterialAsset* material = renderAssetLibrary_.LoadMaterial(materialID);
	if (!material || !context.systemContext || !output || !output->GetUAVGPUHandle().ptr) return false;

	std::unordered_map<std::string, std::string> inputs;
	RenderFeatureResourceUtility::ApplyDefaultSceneInputs(inputs);
	inputs[PostProcessBindingNames::kSourceColor] = RenderTargetNames::kSceneColorFinal;
	inputs[ShaderGraphBindingNames::kSceneColor] = RenderTargetNames::kSceneColorFinal;
	if (material->domain == MaterialDomain::Compute) {
		PostProcessExecutionDesc desc{};
		desc.material = materialID;
		desc.passKind = MaterialPassKind::PostProcess;
		desc.source.colors = {"SceneFinal"};
		desc.dest.colors = {"View"};
		desc.extraSources = std::move(inputs);
		desc.dispatchMode = ComputeDispatchMode::FromDestSize;
		return postProcessExecutor_.Execute(graphicsCore, {}, context, renderAssetLibrary_, pipelineStateCache_, desc);
	}
	if (material->domain != MaterialDomain::RayTracing || !graphicsCore.GetDXObject().ShouldUseDispatchRays()) return false;

	if (!context.raytracing.tlasResource || !context.raytracing.materialTexturesReady) return false;
	RenderFeaturePassSettings pass{};
	pass.name = "ShaderGraphPreview";
	pass.material = materialID;
	pass.type = RenderFeaturePassType::RayTracing;
	pass.materialPass = MaterialPassKind::RayTracing;
	RayTracingExecutionResources resources{};
	resources.inputs = std::move(inputs);
	resources.dispatchTarget = output;
	resources.outputs[PostProcessBindingNames::kDestColor] = output;
	return rayTracingExecutor_.Execute(graphicsCore, context, renderAssetLibrary_, raytracingPipelineStateCache_, pass, resources, nullptr);
}

bool RenderPipelineRunner::RenderEntityPreview(
	GraphicsCore& graphicsCore, const EntityPreviewRenderRequest& request) {

	if (!request.world || !request.assetDatabase || !request.surface || !request.surface->IsValid() ||
		!request.world->IsAlive(request.rootEntity)) {
		return false;
	}

	// メインのScene/Gameとは別に、ツール用サーフェイスだけを描画対象にする
	renderAssetLibrary_.Init(request.assetDatabase);
	previewResources_.BeginFrame(graphicsCore);
	auto& preparation = previewResources_.previewScenePreparation_;
	const bool materialPreview = request.kind != EntityPreviewKind::Model;
	preparation.Extract(*request.world, extractorRegistry_, lightExtractorRegistry_, materialPreview ? &assetReloadService_ : nullptr);

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
	BuildPreviewPassBuckets(*request.world, request.rootEntity, preparation.renderBatch_, previewView, passBuckets, meshAssets);

	RenderTargetRegistry previewTargetRegistry{};
	previewTargetRegistry.BeginFrame();
	previewTargetRegistry.Register("View", request.surface, { "Preview.Color" }, std::string("Preview.Depth"));
	previewTargetRegistry.Register(ViewportRenderService::GetViewAlias(RenderViewKind::Scene),
		request.surface, { "Preview.Color" }, std::string("Preview.Depth"));

	SceneExecutionContext context{};
	context.kind = RenderViewKind::Scene;
	if (request.kind == EntityPreviewKind::MaterialThumbnail) context.previewClearColor = request.clearColor;
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
	context.useThumbnailTextures = request.useThumbnailTextures;
	context.forceVertexMeshVariant = request.forceVertexMeshVariant;
	context.world = request.world;
	context.systemContext = request.systemContext;
	context.assetDatabase = request.assetDatabase;

	// プレビュー用のライトバッファを更新しSceneView/GameViewのGPUバッファは触らない
	previewResources_.previewLightSet_.Clear();
	ViewLightCollector::CollectForView(preparation.frameLightBatch_, &previewScene, previewView, previewResources_.previewLightSet_);
	ViewLightBufferSet& previewLightBuffers = previewResources_.previewLightBufferPool_.Acquire(graphicsCore,
		[](ViewLightBufferSet& buffers, GraphicsCore& core) {
			buffers.Init(core);
		});
	previewLightBuffers.Upload(previewResources_.previewLightSet_);
	previewLightBuffers.RegisterTo(context.bufferRegistry);
	// PreviewのEntity番号へ本描画の塗装を対応付けない
	previewPaint_.Prepare(graphicsCore, request.world, request.assetDatabase, renderAssetLibrary_, request.paintSample, request.systemContext);
	context.materialPaintRevision = previewPaint_.Publish(context.bufferRegistry);
	previewResources_.previewFeedback_.Publish(graphicsCore, context.bufferRegistry);

	auto* meshBackendBase = previewResources_.previewBackendRegistry_.Find(RenderBackendID::Mesh);
	auto* meshBackend = dynamic_cast<MeshRenderBackend*>(meshBackendBase);
	if (meshBackend && !meshAssets.empty()) {

		meshBackend->RequestMeshes(graphicsCore, *request.assetDatabase, meshAssets);
		// 非同期読込中は完了扱いにせず、呼び出し側へ次フレームの再描画を要求させる
		if (!meshBackend->AreMeshesReady(meshAssets)) {
			return false;
		}
		PreDispatchVisibleMeshSkinning(graphicsCore, context,
			preparation.renderBatch_, previewResources_.previewBackendRegistry_, renderAssetLibrary_, pipelineStateCache_, materialResolver_, passBuckets);
	}

	if (materialPreview) {
		auto& view = request.kind == EntityPreviewKind::MaterialThumbnail ? previewResources_.thumbnailView_ :
			request.kind == EntityPreviewKind::ShaderGraph ? previewResources_.graphView_ : previewResources_.materialView_;
		if (!view) {
			view = std::make_unique<RenderPreviewResources::MaterialView>();
			view->color.BeginFrame();
		}
		auto& targets = view->targets;
		const bool firstPreview = !targets.IsValid();
		targets.Resize(graphicsCore, previewView.width, previewView.height);
		if (!targets.IsValid()) return false;
		if (firstPreview) {
			RenderPipelineDeps deps{};
			deps.renderBatch = &preparation.renderBatch_;
			deps.backendRegistry = &previewResources_.previewBackendRegistry_;
			deps.assetLibrary = &renderAssetLibrary_;
			deps.pipelineCache = &pipelineStateCache_;
			deps.materialResolver = &materialResolver_;
			deps.colorPipelineProcessor = &view->color;
			deps.dispatcher = &batchDispatcher_;
			view->path.Initialize(deps, true);
		}
		previewView.historyWorldRevision = UINT64_MAX;
		context.resources = &targets;
		context.viewLights = &previewResources_.previewLightSet_;
		context.postProcessCamera = previewView.perspective;
		context.postProcessCamera.postProcessEnabled = false;
		context.disableMeshCulling = true;
		const MaterialAsset* effect = request.materialPreviewEffect ? renderAssetLibrary_.LoadMaterial(request.materialPreviewEffect) : nullptr;
		if (effect && effect->domain == MaterialDomain::RayTracing && graphicsCore.GetDXObject().ShouldUseDispatchRays()) {
			// DecalPassが命中面の投影候補を準備できるよう、先にTLASを構築する
			auto& scene = previewResources_.effectScene_;
			auto& rayView = previewResources_.effectView_;
			auto* primitives = static_cast<PrimitiveRenderBackend*>(previewResources_.previewBackendRegistry_.Find(RenderBackendID::Primitive));
			scene.BeginFrame(graphicsCore);
			scene.BuildForScene(graphicsCore, *context.assetDatabase, renderAssetLibrary_, materialResolver_,
				meshBackend, primitives ? &primitives->GetGeometryManager() : nullptr, preparation.renderBatch_, context);
			RegisterSceneTLAS(context);
			rayView.Init(graphicsCore);
			rayView.Upload(previewView, {}, graphicsCore.GetDXObject().GetFeatureController().GetPreferences());
			rayView.RegisterTo(context.bufferRegistry);
			context.raytracingViewConstants = &rayView.GetData();
		}
		RegisterSceneMaterialInputs(context);
		view->path.Execute(graphicsCore, passBuckets, context);
		const bool rendered = !request.materialPreviewEffect || DrawPreviewEffect(graphicsCore, context, request.materialPreviewEffect);
		request.surface->TransitionForShaderRead(*graphicsCore.GetDXObject().GetDxCommand());
		previewTargetRegistry.EndFrame();
		return rendered;
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
		batchDispatcher_.Dispatch(graphicsCore, context, preparation.renderBatch_, previewResources_.previewBackendRegistry_,
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
	context.targetRegistry->EndFrame();

	return true;
}
