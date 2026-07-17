#include "UISelectableComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIComponentSerialization.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

//============================================================================
//	UISelectableComponent internal
//============================================================================
namespace {

	void ReadStyle(const nlohmann::json& in, Engine::UITransitionStyle& style) {

		if (!in.is_object()) {
			return;
		}
		if (in.contains("color")) {
			style.color = Engine::Color4::FromJson(in["color"]);
		}
		if (in.contains("scale")) {
			style.scale = Engine::Vector2::FromJson(in["scale"]);
		}
		style.overrideTexture = in.value("overrideTexture", style.overrideTexture);
		style.texture = Engine::ParseAssetID(in, "texture");
	}

	nlohmann::json WriteStyle(const Engine::UITransitionStyle& style) {

		return {
			{ "color",style.color.ToJson() },
			{ "scale",style.scale.ToJson() },
			{ "overrideTexture",style.overrideTexture },
			{ "texture",Engine::ToAssetReferenceJson(style.texture) }
		};
	}
}

//============================================================================
//	UISelectableComponent classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, UISelectableComponent& component) {

	component.interactable = in.value("interactable", component.interactable);
	component.navigationMode = EnumAdapter<UINavigationMode>::FromString(
		in.value("navigationMode", "Automatic")).value_or(component.navigationMode);
	component.targetLocalFileID = UIComponentSerialization::ReadEntityReference(in, "target");
	component.upLocalFileID = UIComponentSerialization::ReadEntityReference(in, "up");
	component.downLocalFileID = UIComponentSerialization::ReadEntityReference(in, "down");
	component.leftLocalFileID = UIComponentSerialization::ReadEntityReference(in, "left");
	component.rightLocalFileID = UIComponentSerialization::ReadEntityReference(in, "right");
	component.useCustomHitArea = in.value("useCustomHitArea", component.useCustomHitArea);
	if (in.contains("hitAreaOffset")) {
		component.hitAreaOffset = Vector2::FromJson(in["hitAreaOffset"]);
	}
	if (in.contains("hitAreaSize")) {
		component.hitAreaSize = Vector2::FromJson(in["hitAreaSize"]);
	}
	component.transitionDuration = in.value("transitionDuration", component.transitionDuration);
	ReadStyle(in.value("normal", nlohmann::json{}), component.normal);
	ReadStyle(in.value("highlighted", nlohmann::json{}), component.highlighted);
	ReadStyle(in.value("pressed", nlohmann::json{}), component.pressed);
	ReadStyle(in.value("selected", nlohmann::json{}), component.selected);
	ReadStyle(in.value("disabled", nlohmann::json{}), component.disabled);
}

void Engine::to_json(nlohmann::json& out, const UISelectableComponent& component) {

	out["interactable"] = component.interactable;
	out["navigationMode"] = EnumAdapter<UINavigationMode>::ToString(component.navigationMode);
	out["target"] = UIComponentSerialization::WriteEntityReference(component.targetLocalFileID);
	out["up"] = UIComponentSerialization::WriteEntityReference(component.upLocalFileID);
	out["down"] = UIComponentSerialization::WriteEntityReference(component.downLocalFileID);
	out["left"] = UIComponentSerialization::WriteEntityReference(component.leftLocalFileID);
	out["right"] = UIComponentSerialization::WriteEntityReference(component.rightLocalFileID);
	out["useCustomHitArea"] = component.useCustomHitArea;
	out["hitAreaOffset"] = component.hitAreaOffset.ToJson();
	out["hitAreaSize"] = component.hitAreaSize.ToJson();
	out["transitionDuration"] = component.transitionDuration;
	out["normal"] = WriteStyle(component.normal);
	out["highlighted"] = WriteStyle(component.highlighted);
	out["pressed"] = WriteStyle(component.pressed);
	out["selected"] = WriteStyle(component.selected);
	out["disabled"] = WriteStyle(component.disabled);
}
