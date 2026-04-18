#include "RenderBackendRegistry.h"

//============================================================================
//	RenderBackendRegistry classMethods
//============================================================================

void Engine::RenderBackendRegistry::BeginFrame(GraphicsCore& graphicsCore) {

	for (auto& [id, backend] : backends_) {

		backend->BeginFrame(graphicsCore);
	}
}

void Engine::RenderBackendRegistry::Register(std::unique_ptr<IRenderBackend> backend) {

	if (!backend) {
		return;
	}
	backends_[backend->GetID()] = std::move(backend);
}

const Engine::IRenderBackend* Engine::RenderBackendRegistry::Find(uint32_t id) const {

	auto it = backends_.find(id);
	if (it == backends_.end()) {
		return nullptr;
	}
	return it->second.get();
}

Engine::IRenderBackend* Engine::RenderBackendRegistry::Find(uint32_t id) {

	auto it = backends_.find(id);
	if (it == backends_.end()) {
		return nullptr;
	}
	return it->second.get();
}

void Engine::RenderBackendRegistry::Clear() {

	backends_.clear();
}