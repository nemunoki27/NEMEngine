#include "UISelectableComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	UISelectableComponent internal
//============================================================================
namespace {

	void ReadStyle(const nlohmann::json& in, Engine::UITransitionStyle& style) {

		if (!in.is_object()) {
			return;
		}
		style.animationEnabled = in.value("animationEnabled", style.animationEnabled);
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
			{ "animationEnabled",style.animationEnabled },
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

}

//============================================================================
//	UISelectableComponent classMethods
//============================================================================
void Engine::UISelectableComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] UISelectableComponent& component) {

	if (!world.HasComponent<UISelectableRuntimeComponent>(entity)) {
		world.AddComponent<UISelectableRuntimeComponent>(entity);
	}
}

void Engine::UISelectableComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	if (world.HasComponent<UISelectableRuntimeComponent>(entity)) {
		world.RemoveComponent<UISelectableRuntimeComponent>(entity);
	}
}

void Engine::UISelectableComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UISelectableComponent& component) {
}

void Engine::UISelectableComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UISelectableComponent& component) {
}

void Engine::UISelectableComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, UISelectableComponent& component) {

	from_json(in, component);
}

void Engine::UISelectableComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const UISelectableComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

void Engine::ApplyUISelectableAuthoring(const UISelectableComponent& source, UISelectableComponent& destination) {

	destination.interactable = source.interactable;
	destination.normal = source.normal;
	destination.selected = source.selected;
	destination.submitted = source.submitted;
	destination.disabled = source.disabled;
}

void Engine::from_json(const nlohmann::json& in, UISelectableComponent& component) {

	component.interactable = in.value("interactable", component.interactable);
	ReadStyle(in.value("normal", nlohmann::json{}), component.normal);
	ReadStyle(in.value("selected", nlohmann::json{}), component.selected);
	ReadStyle(in.value("submitted", nlohmann::json{}), component.submitted);
	ReadStyle(in.value("disabled", nlohmann::json{}), component.disabled);
}

void Engine::to_json(nlohmann::json& out, const UISelectableComponent& component) {

	out["interactable"] = component.interactable;
	out["normal"] = WriteStyle(component.normal);
	out["selected"] = WriteStyle(component.selected);
	out["submitted"] = WriteStyle(component.submitted);
	out["disabled"] = WriteStyle(component.disabled);
}
