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
	overrides_[passID].enabled = enabled;
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::SetSceneColorOutput(
	const RenderFeatureProfileAsset& profile, UUID passID, bool enabled) {

	const auto selected = std::find_if(profile.passes.begin(), profile.passes.end(),
		[passID](const RenderFeaturePassSettings& pass) { return pass.id == passID; });
	if (!passID || selected == profile.passes.end()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[レンダー機能] SceneColor出力を変更するパスが存在しません");
		return false;
	}
	if (enabled) {

		// 不正な出力では現在の出力指定を変更しない
		const RenderFeatureOutputSettings output = selected->outputs.empty() ?
			RenderFeatureOutputSettings{} : selected->outputs.front();
		if (output.format != RenderFeatureTextureFormat::Inherit ||
			output.widthScale != 1.0f || output.heightScale != 1.0f) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[レンダー機能] SceneColor出力は継承形式かつ等倍が必要です パス={}", selected->name);
			return false;
		}
		// 出力指定だけを切り替え、各パスの有効状態は保持
		for (const RenderFeaturePassSettings& pass : profile.passes) {

			if (pass.id != passID && pass.anchor == selected->anchor) {

				overrides_[pass.id].sceneColorOutput = false;
			}
		}
	}
	overrides_[passID].sceneColorOutput = enabled;
	return true;
}

bool Engine::RenderFeatureRuntimeOverrides::IsEnabled(
	UUID passID, bool savedValue) const {

	const RenderFeaturePassRuntimeOverride* value = Find(passID);
	return value ? value->enabled.value_or(savedValue) : savedValue;
}

bool Engine::RenderFeatureRuntimeOverrides::IsSceneColorOutput(
	UUID passID, bool savedValue) const {

	const RenderFeaturePassRuntimeOverride* value = Find(passID);
	return value ? value->sceneColorOutput.value_or(savedValue) : savedValue;
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
		!pass->second.sceneColorOutput.has_value() &&
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
