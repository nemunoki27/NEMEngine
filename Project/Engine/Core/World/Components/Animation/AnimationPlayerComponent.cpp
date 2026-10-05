#include "AnimationPlayerComponent.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>

//============================================================================
//	AnimationPlayerComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, AnimationPlayerComponent& component) {

	// 保存対象だけを読み込み、実行状態は持ち越さない
	component = AnimationPlayerComponent{};
	component.enabled = in.value("enabled", true);
	component.defaultGroup = in.value("defaultGroup", std::string());
	component.playOnStart = in.value("playOnStart", true);
	component.playInEditMode = in.value("playInEditMode", false);
	component.globalSpeed = in.value("globalSpeed", 1.0f);
	component.controller = ParseAssetID(in, "animationController");

	component.groups.clear();
	if (in.contains("groups") && in["groups"].is_array()) {
		for (const auto& groupJson : in["groups"]) {
			component.groups.push_back(LoadAnimationGroup(groupJson));
		}
	}

}

void Engine::to_json(nlohmann::json& out, const AnimationPlayerComponent& component) {

	out["enabled"] = component.enabled;
	out["defaultGroup"] = component.defaultGroup;
	out["playOnStart"] = component.playOnStart;
	out["playInEditMode"] = component.playInEditMode;
	out["globalSpeed"] = component.globalSpeed;
	out["animationController"] = ToAssetReferenceJson(component.controller);

	out["groups"] = nlohmann::json::array();
	for (const auto& group : component.groups) {
		out["groups"].push_back(SaveAnimationGroup(group));
	}
}
