#include "ParticlePrimitiveShapeDrawerRegistry.h"

//============================================================================
//	ParticlePrimitiveShapeDrawerRegistry classMethods
//============================================================================
Engine::ParticlePrimitiveShapeDrawerRegistry& Engine::ParticlePrimitiveShapeDrawerRegistry::GetInstance() {

	static ParticlePrimitiveShapeDrawerRegistry instance;
	return instance;
}

uint32_t Engine::ParticlePrimitiveShapeDrawerRegistry::Register(
	PrimitiveType type, std::unique_ptr<IParticlePrimitiveShapeDrawer> instance) {

	// 既に登録済みならそのまま返す
	auto it = drawers_.find(type);
	if (it != drawers_.end()) {
		return static_cast<uint32_t>(drawers_.size());
	}

	drawers_[type] = std::move(instance);
	return static_cast<uint32_t>(drawers_.size());
}

const Engine::IParticlePrimitiveShapeDrawer* Engine::ParticlePrimitiveShapeDrawerRegistry::Find(PrimitiveType type) const {

	auto it = drawers_.find(type);
	if (it == drawers_.end()) {
		return nullptr;
	}
	return it->second.get();
}
