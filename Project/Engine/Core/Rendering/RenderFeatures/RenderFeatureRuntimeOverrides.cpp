#include "RenderFeatureRuntimeOverrides.h"

//============================================================================
//	RenderFeatureRuntimeOverrides classMethods
//============================================================================
bool Engine::RenderFeatureRuntimeOverrides::SetEnabled(
	UUID passID, bool enabled) {

	if (!passID) {
		return false;
	}
	overrides_[passID].enabled = enabled;
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::SetGroupEnabled(
	std::string_view groupName, bool enabled) {

	if (groupName.empty()) {
		return false;
	}
	groupEnabledOverrides_[std::string(groupName)] = enabled;
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::SetParameter(
	UUID passID, MaterialParameterID parameterID,
	std::string_view parameterName, const MaterialParameterValue& value) {

	if (!passID || parameterName.empty() || !parameterID) {
		return false;
	}
	RenderFeaturePassRuntimeOverride& pass =
		overrides_[passID];
	pass.parameters.Set(parameterID, parameterName,
		ResolveMaterialParameterSemantic(parameterName), value);
	if (const AssetID* texture = std::get_if<AssetID>(&value.value)) {
		pass.textureOverrides[std::string(parameterName)] = *texture;
	} else {
		pass.textureOverrides.erase(std::string(parameterName));
	}
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::ClearParameter(
	UUID passID, MaterialParameterID parameterID) {

	if (!passID || !parameterID) {
		return false;
	}
	const auto pass = overrides_.find(passID);
	if (pass == overrides_.end()) {

		return false;
	}
	std::string parameterName{};
	for (const MaterialParameterRecord& parameter :
		pass->second.parameters.GetRecords()) {

		if (parameter.id == parameterID) {
			parameterName = parameter.namedValue.first;
			break;
		}
	}
	const bool removed =
		pass->second.parameters.erase(parameterID) != 0;
	if (!parameterName.empty()) {
		pass->second.textureOverrides.erase(parameterName);
	}
	if (!removed) {
		return false;
	}
	if (!pass->second.enabled.has_value() &&
		pass->second.parameters.empty() &&
		pass->second.textureOverrides.empty()) {

		overrides_.erase(pass);
	}
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::ResetPass(
	UUID passID) {

	return passID && overrides_.erase(passID) != 0;
}

void Engine::RenderFeatureRuntimeOverrides::ResetAll() {

	overrides_.clear();
	groupEnabledOverrides_.clear();
}

const Engine::RenderFeaturePassRuntimeOverride*
Engine::RenderFeatureRuntimeOverrides::Find(
	UUID passID) const {

	const auto pass = overrides_.find(passID);
	return pass == overrides_.end() ? nullptr : &pass->second;
}

bool Engine::RenderFeatureRuntimeOverrides::IsGroupEnabled(
	std::string_view groupName, bool fallback) const {

	const auto group = groupEnabledOverrides_.find(std::string(groupName));
	return group == groupEnabledOverrides_.end() ? fallback : group->second;
}

Engine::RenderFeatureRuntimeOverrides&
Engine::RenderFeatureRuntimeOverrides::GetInstance() {

	static RenderFeatureRuntimeOverrides instance;
	return instance;
}
