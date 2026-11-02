#include "UISelectableComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	UISelectableComponent internal
//============================================================================
namespace {

	// 保存された状態別の表示設定を読む
	void ReadStyle(const nlohmann::json& in, Engine::UITransitionStyle& style) {

		if (!in.is_object()) {
			return;
		}
		style.animationEnabled = in.value("animationEnabled", style.animationEnabled);
		if (in.contains("colorAnimation")) style.colorAnimation = in.at("colorAnimation").get<Engine::SimpleAnimationSettings>();
		else {
			if (in.contains("color")) style.colorAnimation.end = Engine::Color4::FromJson(in.at("color"));
			style.colorAnimation.duration = in.value("colorTransitionDuration", 0.08);
			style.colorAnimation.easing = Engine::EnumAdapter<EasingType>::FromString(in.value("colorEasing", "Linear")).value_or(EasingType::Linear);
		}
		if (in.contains("scaleAnimation")) style.scaleAnimation = in.at("scaleAnimation").get<Engine::SimpleAnimationSettings>();
		else {
			if (in.contains("scale")) style.scaleAnimation.end = Engine::Vector2::FromJson(in.at("scale"));
			style.scaleAnimation.duration = in.value("scaleTransitionDuration", 0.08);
			style.scaleAnimation.easing = Engine::EnumAdapter<EasingType>::FromString(in.value("scaleEasing", "Linear")).value_or(EasingType::Linear);
		}
		if (style.colorAnimation.start.index() != 5 || style.scaleAnimation.start.index() != 1)
			throw std::invalid_argument("UISelectableの色・スケールの型が不正です");
		if (in.value("useAnimationClip", false)) {
			Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::warn,
				"[UISelectable] 旧AnimationClip設定をSimpleAnimationの色・スケール設定へ変更してください");
		}
		style.overrideTexture = in.value("overrideTexture", style.overrideTexture);
		style.texture = Engine::ParseAssetID(in, "texture");
		style.sound = Engine::ParseAssetID(in, "sound");
		style.soundVolume = in.value("soundVolume", style.soundVolume);
	}

	// 状態別の表示設定を保存形式へ変換
	nlohmann::json WriteStyle(const Engine::UITransitionStyle& style) {

		return {
			{ "animationEnabled",style.animationEnabled },
			{ "colorAnimation",style.colorAnimation },
			{ "scaleAnimation",style.scaleAnimation },
			{ "overrideTexture",style.overrideTexture },
			{ "texture",Engine::ToAssetReferenceJson(style.texture) },
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

	// 表示の実行状態を追加
	if (!world.HasComponent<UISelectableRuntimeComponent>(entity)) {
		world.AddComponent<UISelectableRuntimeComponent>(entity);
	}
}

void Engine::UISelectableComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	// 表示の実行状態を削除
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

	// 保存設定を読み込む
	from_json(in, component);
}

void Engine::UISelectableComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const UISelectableComponent& component, nlohmann::json& out) {

	// 選択表示の設定を保存
	to_json(out, component);
}

void Engine::ApplyUISelectableAuthoring(const UISelectableComponent& source, UISelectableComponent& destination) {

	// 保存設定を選択表示へ反映
	destination.interactable = source.interactable;
	destination.normal = source.normal;
	destination.selected = source.selected;
	destination.submitted = source.submitted;
	destination.disabled = source.disabled;
}

void Engine::from_json(const nlohmann::json& in, UISelectableComponent& component) {

	// 状態別の色と形状と参照を読む
	component.interactable = in.value("interactable", component.interactable);
	ReadStyle(in.value("normal", nlohmann::json{}), component.normal);
	ReadStyle(in.value("selected", nlohmann::json{}), component.selected);
	ReadStyle(in.value("submitted", nlohmann::json{}), component.submitted);
	ReadStyle(in.value("disabled", nlohmann::json{}), component.disabled);
}

void Engine::to_json(nlohmann::json& out, const UISelectableComponent& component) {

	// 状態別の表示設定をJSONへ書き出す
	out["interactable"] = component.interactable;
	out["normal"] = WriteStyle(component.normal);
	out["selected"] = WriteStyle(component.selected);
	out["submitted"] = WriteStyle(component.submitted);
	out["disabled"] = WriteStyle(component.disabled);
}

Engine::SimpleAnimationSettings Engine::MakeUIAnimationSettings(const SimpleAnimationValue& target) {

	auto settings = MakeSimpleAnimationSettings(target.index());
	settings.start = settings.end = settings.center = target;
	settings.duration = 0.08;
	settings.blendDuration = 0.08;
	settings.startFromCurrent = true;
	settings.unscaledTime = true;
	return settings;
}
