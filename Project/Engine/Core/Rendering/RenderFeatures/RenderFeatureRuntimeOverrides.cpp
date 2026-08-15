#include "RenderFeatureRuntimeOverrides.h"

//============================================================================
//	RenderFeatureRuntimeOverrides classMethods
//============================================================================
bool Engine::RenderFeatureRuntimeOverrides::SetEnabled(
	std::string_view passName, bool enabled) {

	if (passName.empty()) {
		return false;
	}
	overrides_[std::string(passName)].enabled = enabled;
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::SetParameter(
	std::string_view passName, MaterialParameterID parameterID,
	std::string_view parameterName, const MaterialParameterValue& value) {

	if (passName.empty() || parameterName.empty() || !parameterID) {
		return false;
	}
	RenderFeaturePassRuntimeOverride& pass =
		overrides_[std::string(passName)];
	pass.parameters.Set(parameterID, parameterName,
		ResolveMaterialParameterSemantic(parameterName), value);
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::ClearParameter(
	std::string_view passName, MaterialParameterID parameterID) {

	if (passName.empty() || !parameterID) {
		return false;
	}
	const auto pass = overrides_.find(std::string(passName));
	if (pass == overrides_.end() ||
		pass->second.parameters.erase(parameterID) == 0) {

		return false;
	}
	if (!pass->second.enabled.has_value() &&
		pass->second.parameters.empty()) {

		overrides_.erase(pass);
	}
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::ResetPass(
	std::string_view passName) {

	return !passName.empty() &&
		overrides_.erase(std::string(passName)) != 0;
}

void Engine::RenderFeatureRuntimeOverrides::ResetAll() {

	overrides_.clear();
}

const Engine::RenderFeaturePassRuntimeOverride*
Engine::RenderFeatureRuntimeOverrides::Find(
	std::string_view passName) const {

	const auto pass = overrides_.find(std::string(passName));
	return pass == overrides_.end() ? nullptr : &pass->second;
}

Engine::RenderFeatureRuntimeOverrides&
Engine::RenderFeatureRuntimeOverrides::GetInstance() {

	static RenderFeatureRuntimeOverrides instance;
	return instance;
}
