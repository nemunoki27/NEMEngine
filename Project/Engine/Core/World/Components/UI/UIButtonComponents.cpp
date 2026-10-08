//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace {

	// ボタンの有効状態とAction名を読み込む
	template <typename TComponent>
	void ReadButton(const nlohmann::json& in, TComponent& component) {

		component.enabled = in.value("enabled", component.enabled);
		component.actionName = in.value("actionName", component.actionName);
	}

	// ボタンの設定だけを保存する
	template <typename TComponent>
	void WriteButton(nlohmann::json& out, const TComponent& component) {

		out["enabled"] = component.enabled;
		out["actionName"] = component.actionName;
	}
}

void Engine::UIImageButtonComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] UIImageButtonComponent& component) {

	// クリックの実行状態を追加
	if (!world.HasComponent<UIImageButtonRuntimeComponent>(entity)) {
		world.AddComponent<UIImageButtonRuntimeComponent>(entity);
	}
}

void Engine::UIImageButtonComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	// クリックの実行状態を削除
	if (world.HasComponent<UIImageButtonRuntimeComponent>(entity)) {
		world.RemoveComponent<UIImageButtonRuntimeComponent>(entity);
	}
}

void Engine::UIImageButtonComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UIImageButtonComponent& component) {
}

void Engine::UIImageButtonComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UIImageButtonComponent& component) {
}

void Engine::UIImageButtonComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, UIImageButtonComponent& component) {

	// 保存された設定を反映
	from_json(in, component);
}

void Engine::UIImageButtonComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const UIImageButtonComponent& component, nlohmann::json& out) {

	// 編集用の設定を保存
	to_json(out, component);
}

void Engine::UITextButtonComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] UITextButtonComponent& component) {

	// クリックの実行状態を追加
	if (!world.HasComponent<UITextButtonRuntimeComponent>(entity)) {
		world.AddComponent<UITextButtonRuntimeComponent>(entity);
	}
}

void Engine::UITextButtonComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

	// クリックの実行状態を削除
	if (world.HasComponent<UITextButtonRuntimeComponent>(entity)) {
		world.RemoveComponent<UITextButtonRuntimeComponent>(entity);
	}
}

void Engine::UITextButtonComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UITextButtonComponent& component) {
}

void Engine::UITextButtonComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] UITextButtonComponent& component) {
}

void Engine::UITextButtonComponent::DeserializeECS(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, UITextButtonComponent& component) {

	// 保存された設定を反映
	from_json(in, component);
}

void Engine::UITextButtonComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const UITextButtonComponent& component, nlohmann::json& out) {

	// 編集用の設定を保存
	to_json(out, component);
}

//============================================================================
//	UIButtonComponent serialization
//============================================================================
void Engine::from_json(const nlohmann::json& in, UIImageButtonComponent& component) {

	// 共通の読込処理へ渡す
	ReadButton(in, component);
}

void Engine::to_json(nlohmann::json& out, const UIImageButtonComponent& component) {

	// 共通の保存処理へ渡す
	WriteButton(out, component);
}

void Engine::from_json(const nlohmann::json& in, UITextButtonComponent& component) {

	// 共通の読込処理へ渡す
	ReadButton(in, component);
}

void Engine::to_json(nlohmann::json& out, const UITextButtonComponent& component) {

	// 共通の保存処理へ渡す
	WriteButton(out, component);
}
