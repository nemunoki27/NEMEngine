#include "ParticleCustomDataModule.h"

void Engine::ParticleCustomDataModule::FromJson(const nlohmann::json& params) {

	from_json(params, settings_);
}

nlohmann::json Engine::ParticleCustomDataModule::ToJson() const {

	nlohmann::json params;
	to_json(params, settings_);
	return params;
}
