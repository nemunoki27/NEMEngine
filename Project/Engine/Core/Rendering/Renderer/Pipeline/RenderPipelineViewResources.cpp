#include "RenderPipelineViewResources.h"

void Engine::RenderPipelineViewResources::EnsureLightBuffers(GraphicsCore& graphicsCore) {

	if (!lightBuffers.IsInitialized()) {
		lightBuffers.Init(graphicsCore);
	}
}

void Engine::RenderPipelineViewResources::EnsureRaytracingBuffers(GraphicsCore& graphicsCore) {

	if (!raytracingBuffers.IsInitialized()) {
		raytracingBuffers.Init(graphicsCore);
	}
}
