#include "ParticleModuleRegistry.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>

//============================================================================
//	ParticleModuleRegistry classMethods
//============================================================================
Engine::ParticleModuleRegistry& Engine::ParticleModuleRegistry::GetInstance() {

	static ParticleModuleRegistry instance;
	return instance;
}

uint32_t Engine::ParticleModuleRegistry::Register(const std::string& id, CreateFunc create) {

	// 既に登録済みならそのまま返す
	auto it = creators_.find(id);
	if (it != creators_.end()) {
		return static_cast<uint32_t>(creators_.size());
	}

	creators_[id] = create;
	return static_cast<uint32_t>(creators_.size());
}

std::unique_ptr<Engine::IParticleModule> Engine::ParticleModuleRegistry::Create(const std::string& id) const {

	auto it = creators_.find(id);
	if (it == creators_.end()) {
		return nullptr;
	}
	return it->second();
}

std::vector<std::string> Engine::ParticleModuleRegistry::GetRegisteredIDs() const {

	std::vector<std::string> ids;
	ids.reserve(creators_.size());
	for (const auto& [id, create] : creators_) {
		ids.emplace_back(id);
	}
	std::sort(ids.begin(), ids.end());
	return ids;
}
