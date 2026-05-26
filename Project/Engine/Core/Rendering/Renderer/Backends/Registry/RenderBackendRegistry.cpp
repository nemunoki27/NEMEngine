#include "RenderBackendRegistry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>

//============================================================================
//	RenderBackendRegistry classMethods
//============================================================================

void Engine::RenderBackendRegistry::BeginFrame(GraphicsCore& graphicsCore) {

	for (auto& [id, backend] : items_) {

		backend->BeginFrame(graphicsCore);
	}
}

void Engine::RenderBackendRegistry::Register(std::unique_ptr<IRenderBackend> backend) {

	if (backend) {
		items_[backend->GetID()] = std::move(backend);
	}
}