#include "ParticleCustomShaderParameterModule.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <string_view>

//============================================================================
//	ParticleCustomShaderParameterModule internal
//============================================================================

//============================================================================
//	ParticleCustomShaderParameterModule classMethods
//============================================================================
void Engine::ParticleCustomShaderParameterModule::FromJson(const nlohmann::json& params) {

	parameters_.clear();
	const auto it = params.find("parameters");
	if (it == params.end() || !it->is_object()) {
		return;
	}
	for (auto parameterIt = it->begin(); parameterIt != it->end(); ++parameterIt) {

		ParticleMaterialAnimatedParameter parameter{};
		from_json(parameterIt.value(), parameter);
		parameters_[parameterIt.key()] = std::move(parameter);
	}
}

nlohmann::json Engine::ParticleCustomShaderParameterModule::ToJson() const {

	nlohmann::json params = nlohmann::json::object();
	params["parameters"] = nlohmann::json::object();
	for (const auto& [name, parameter] : parameters_) {
		to_json(params["parameters"][name], parameter);
	}
	return params;
}

void Engine::ParticleCustomShaderParameterModule::SetParameters(
	const std::unordered_map<std::string, ParticleMaterialAnimatedParameter>& parameters) {

	parameters_ = parameters;
}
