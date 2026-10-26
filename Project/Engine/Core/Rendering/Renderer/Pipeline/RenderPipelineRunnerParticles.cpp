#include "RenderPipelineRunner.h"

//============================================================================
//	include
//============================================================================
#include "RenderPipelineUtility.h"
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderBackend.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshRenderBackend.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

Engine::ParticleGPUFrameInputs Engine::RenderPipelineRunner::PrepareParticleFrameInputs(GraphicsCore& graphics,
	const RenderFrameRequest& request, const SceneInstance* scene) {

	RenderPipelineDeps deps;
	deps.renderBatch = &scenePreparation_.renderBatch_;
	deps.backendRegistry = &backendRegistry_;
	deps.assetLibrary = &renderAssetLibrary_;
	deps.pipelineCache = &pipelineStateCache_;
	deps.materialResolver = &materialResolver_;
	deps.dispatcher = &batchDispatcher_;
	RenderBufferRegistry sharedBuffers;
	particleBackend_->PublishFeedback(graphics, sharedBuffers);
	auto inputs = particleCameraDepth_.Prepare(graphics, request, scene, gameCameraViews_,
		gameViewState_.view.perspective.sourceCamera, deps, sharedBuffers);
	// 非表示でも発生元Cameraの描画入力だけは更新する
	const auto* gameRequest = request.FindView(RenderViewKind::Game);
	std::unordered_set<AssetID> sourceMeshes;
	for (const auto& view : gameCameraViews_) {
		if (!view.valid || !particleCameraDepth_.NeedsCapture(view.perspective.sourceCamera) ||
			!gameRequest || gameRequest->ShouldRender(view)) continue;
		if (!view.targetTexture) viewportRenderService_->SyncSurface(graphics, RenderViewKind::Game,
			gameRequest->width, gameRequest->height);
		if (scene) CollectVisibleMeshAssetsForView(scenePreparation_.renderBatch_, scene->instanceID, view, sourceMeshes);
	}
	if (meshBackend_ && request.assetDatabase && !sourceMeshes.empty()) {
		const std::vector<AssetID> meshes(sourceMeshes.begin(), sourceMeshes.end());
		meshBackend_->RequestMeshes(graphics, *request.assetDatabase, meshes);
	}
	if (request.systemContext) inputs.deltaTime = request.systemContext->mode == WorldMode::Play ?
		request.systemContext->deltaTime : request.systemContext->unscaledDeltaTime;
	if (request.sceneInstances) {
		for (const auto& instance : request.sceneInstances->GetAll()) inputs.sceneInstances.push_back(instance.instanceID);
	}
	return inputs;
}
