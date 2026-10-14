#include "RenderFeatureRuntimeOverrides.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>

// c++
#include <algorithm>

//============================================================================
//	RenderFeatureRuntimeOverrides classMethods
//============================================================================
bool Engine::RenderFeatureRuntimeOverrides::SetEnabled(
	UUID passID, bool enabled) {

	if (!passID) {
		return false;
	}
	auto& value = overrides_[passID].enabled;
	if (value != enabled) {
		value = enabled;
		++planRevision_;
	}
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::IsEnabled(
	UUID passID, bool savedValue) const {

	const RenderFeaturePassRuntimeOverride* value = Find(passID);
	return value ? value->enabled.value_or(savedValue) : savedValue;
}

bool Engine::RenderFeatureRuntimeOverrides::SetGroupEnabled(
	std::string_view groupName, bool enabled) {

	if (groupName.empty()) {
		return false;
	}
	const std::string name(groupName);
	const auto previous = groupEnabledOverrides_.find(name);
	if (previous == groupEnabledOverrides_.end() || previous->second != enabled) {
		groupEnabledOverrides_.insert_or_assign(name, enabled);
		++planRevision_;
	}
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

	const auto found = overrides_.find(passID);
	if (!passID || found == overrides_.end()) return false;
	if (found->second.enabled.has_value()) ++planRevision_;
	overrides_.erase(found);
	return true;
}

void Engine::RenderFeatureRuntimeOverrides::ResetAll() {

	if (!overrides_.empty() || !viewOverrides_.empty() || !groupEnabledOverrides_.empty()) ++planRevision_;
	overrides_.clear();
	viewOverrides_.clear();
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

void Engine::RenderFeatureRuntimeOverrides::SetViewEnabled(AssetID profile, bool gameView, bool sceneView) {

	if (!profile) return;
	const std::pair<bool, bool> value{gameView, sceneView};
	const auto previous = viewOverrides_.find(profile);
	if (previous == viewOverrides_.end() || previous->second != value) {
		viewOverrides_.insert_or_assign(profile, value);
		++planRevision_;
	}
}

bool Engine::RenderFeatureRuntimeOverrides::IsViewEnabled(AssetID profile, bool sceneView, bool savedValue) const {

	const auto value = viewOverrides_.find(profile);
	return value == viewOverrides_.end() ? savedValue : sceneView ? value->second.second : value->second.first;
}
