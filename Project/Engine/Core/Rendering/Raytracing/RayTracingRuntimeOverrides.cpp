#include "RayTracingRuntimeOverrides.h"

//============================================================================
//	RayTracingRuntimeOverrides classMethods
//============================================================================
bool Engine::RayTracingRuntimeOverrides::SetEnabled(
	std::string_view effectName, bool enabled) {

	if (effectName.empty()) {
		return false;
	}
	overrides_[std::string(effectName)].enabled = enabled;
	return true;
}

bool Engine::RayTracingRuntimeOverrides::SetParameter(
	std::string_view effectName, MaterialParameterID parameterID,
	std::string_view parameterName, const MaterialParameterValue& value) {

	if (effectName.empty() || parameterName.empty() || !parameterID) {
		return false;
	}
	RayTracingEffectRuntimeOverride& effect = overrides_[std::string(effectName)];
	effect.parameters.Set(parameterID, parameterName,
		ResolveMaterialParameterSemantic(parameterName), value);
	return true;
}

bool Engine::RayTracingRuntimeOverrides::ClearParameter(
	std::string_view effectName, MaterialParameterID parameterID) {

	if (effectName.empty() || !parameterID) {
		return false;
	}
	const auto effect = overrides_.find(std::string(effectName));
	if (effect == overrides_.end() ||
		effect->second.parameters.erase(parameterID) == 0) {

		return false;
	}
	if (!effect->second.enabled.has_value() &&
		effect->second.parameters.empty()) {

		overrides_.erase(effect);
	}
	return true;
}

bool Engine::RayTracingRuntimeOverrides::ResetEffect(
	std::string_view effectName) {

	return !effectName.empty() &&
		overrides_.erase(std::string(effectName)) != 0;
}

void Engine::RayTracingRuntimeOverrides::ResetAll() {

	overrides_.clear();
}

const Engine::RayTracingEffectRuntimeOverride*
Engine::RayTracingRuntimeOverrides::Find(const std::string& effectName) const {

	const auto effect = overrides_.find(effectName);
	return effect != overrides_.end() ? &effect->second : nullptr;
}

Engine::RayTracingRuntimeOverrides&
Engine::RayTracingRuntimeOverrides::GetInstance() {

	static RayTracingRuntimeOverrides instance;
	return instance;
}
