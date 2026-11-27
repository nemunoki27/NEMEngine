#include "RenderPreviewResources.h"

Engine::RenderPreviewResources::MaterialView::~MaterialView() {

	path.Finalize();
	targets.Destroy();
	color.Release();
}

void Engine::RenderPreviewResources::BeginFrame(GraphicsCore& graphicsCore) {

	if (!previewBackendFrameStarted_) {

		previewBackendRegistry_.BeginFrame(graphicsCore);
		previewLightBufferPool_.BeginFrame();
		if (materialView_) materialView_->color.BeginFrame();
		if (thumbnailView_) thumbnailView_->color.BeginFrame();
		if (graphView_) graphView_->color.BeginFrame();
		previewBackendFrameStarted_ = true;
	}
}
