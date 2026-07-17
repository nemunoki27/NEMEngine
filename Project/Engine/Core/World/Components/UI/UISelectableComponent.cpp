#include "UISelectableComponent.h"

//============================================================================
//	include
//============================================================================
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
		style.colorTransitionDuration = in.value("colorTransitionDuration", style.colorTransitionDuration);
		style.colorEasing = Engine::EnumAdapter<EasingType>::FromString(
			in.value("colorEasing", "Linear")).value_or(style.colorEasing);
		if (in.contains("scale")) {
			style.scale = Engine::Vector2::FromJson(in["scale"]);
		}
		style.scaleTransitionDuration = in.value("scaleTransitionDuration", style.scaleTransitionDuration);
		style.scaleEasing = Engine::EnumAdapter<EasingType>::FromString(
			in.value("scaleEasing", "Linear")).value_or(style.scaleEasing);
		style.overrideTexture = in.value("overrideTexture", style.overrideTexture);
		style.texture = Engine::ParseAssetID(in, "texture");
		style.useAnimationClip = in.value("useAnimationClip", style.useAnimationClip);
		style.animationClip = Engine::ParseAssetID(in, "animationClip");
		style.sound = Engine::ParseAssetID(in, "sound");
		style.soundVolume = in.value("soundVolume", style.soundVolume);
	}

	nlohmann::json WriteStyle(const Engine::UITransitionStyle& style) {

		return {
			{ "color",style.color.ToJson() },
			{ "colorTransitionDuration",style.colorTransitionDuration },
			{ "colorEasing",Engine::EnumAdapter<EasingType>::ToString(style.colorEasing) },
			{ "scale",style.scale.ToJson() },
			{ "scaleTransitionDuration",style.scaleTransitionDuration },
			{ "scaleEasing",Engine::EnumAdapter<EasingType>::ToString(style.scaleEasing) },
			{ "overrideTexture",style.overrideTexture },
			{ "texture",Engine::ToAssetReferenceJson(style.texture) },
			{ "useAnimationClip",style.useAnimationClip },
			{ "animationClip",Engine::ToAssetReferenceJson(style.animationClip) },
			{ "sound",Engine::ToAssetReferenceJson(style.sound) },
			{ "soundVolume",style.soundVolume }
		};
	}

	void ReadSubmitKeys(const nlohmann::json& in, std::vector<KeyDIKCode>& keys) {

		if (!in.is_array()) {
			return;
		}
		keys.clear();
		for (const nlohmann::json& value : in) {

			if (!value.is_number_integer()) {
				continue;
			}
			const int32_t code = value.get<int32_t>();
			if (0 < code && code <= 255) {
				keys.emplace_back(static_cast<KeyDIKCode>(code));
			}
		}
	}

	void ReadSubmitGamepadButtons(const nlohmann::json& in, std::vector<GamePadButtons>& buttons) {

		if (!in.is_array()) {
			return;
		}
		buttons.clear();
		for (const nlohmann::json& value : in) {

			if (!value.is_number_integer()) {
				continue;
			}
			const int32_t code = value.get<int32_t>();
			if (0 <= code && code < static_cast<int32_t>(GamePadButtons::Counts)) {
				buttons.emplace_back(static_cast<GamePadButtons>(code));
			}
		}
	}

	template <typename T>
	nlohmann::json WriteInputBindings(const std::vector<T>& bindings) {

		nlohmann::json out = nlohmann::json::array();
		for (T binding : bindings) {
			out.emplace_back(static_cast<int32_t>(binding));
		}
		return out;
	}
}

//============================================================================
//	UISelectableComponent classMethods
//============================================================================
void Engine::ApplyUISelectableAuthoring(const UISelectableComponent& source, UISelectableComponent& destination) {

	destination.interactable = source.interactable;
	destination.normal = source.normal;
	destination.selected = source.selected;
	destination.submitted = source.submitted;
	destination.disabled = source.disabled;
	destination.submitKeys = source.submitKeys;
	destination.submitGamepadButtons = source.submitGamepadButtons;
}

void Engine::from_json(const nlohmann::json& in, UISelectableComponent& component) {

	component.interactable = in.value("interactable", component.interactable);
	ReadStyle(in.value("normal", nlohmann::json{}), component.normal);
	ReadStyle(in.value("selected", nlohmann::json{}), component.selected);
	ReadStyle(in.value("submitted", nlohmann::json{}), component.submitted);
	ReadStyle(in.value("disabled", nlohmann::json{}), component.disabled);
	ReadSubmitKeys(in.value("submitKeys", nlohmann::json{}), component.submitKeys);
	ReadSubmitGamepadButtons(in.value("submitGamepadButtons", nlohmann::json{}),
		component.submitGamepadButtons);
}

void Engine::to_json(nlohmann::json& out, const UISelectableComponent& component) {

	out["interactable"] = component.interactable;
	out["normal"] = WriteStyle(component.normal);
	out["selected"] = WriteStyle(component.selected);
	out["submitted"] = WriteStyle(component.submitted);
	out["disabled"] = WriteStyle(component.disabled);
	out["submitKeys"] = WriteInputBindings(component.submitKeys);
	out["submitGamepadButtons"] = WriteInputBindings(component.submitGamepadButtons);
}
