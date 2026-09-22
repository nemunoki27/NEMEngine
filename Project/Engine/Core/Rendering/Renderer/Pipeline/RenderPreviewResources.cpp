#include "RenderPreviewResources.h"

void Engine::RenderPreviewResources::BeginFrame(GraphicsCore& graphicsCore) {

	if (!previewBackendFrameStarted_) {

		previewBackendRegistry_.BeginFrame(graphicsCore);
		previewLightBufferPool_.BeginFrame();
		previewBackendFrameStarted_ = true;
	}
}
