//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace {

	template <typename TComponent>
	void ReadButton(const nlohmann::json& in, TComponent& component) {

		component.enabled = in.value("enabled", component.enabled);
		component.actionName = in.value("actionName", component.actionName);
	}

	template <typename TComponent>
	void WriteButton(nlohmann::json& out, const TComponent& component) {

		out["enabled"] = component.enabled;
		out["actionName"] = component.actionName;
	}
}

void Engine::UIImageButtonComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] UIImageButtonComponent& component) {

	if (!world.HasComponent<UIImageButtonRuntimeComponent>(entity)) {
		world.AddComponent<UIImageButtonRuntimeComponent>(entity);
	}
}

void Engine::UIImageButtonComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

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

	from_json(in, component);
}

void Engine::UIImageButtonComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const UIImageButtonComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

void Engine::UITextButtonComponent::OnAdded(
	ECSWorld& world, const Entity& entity,
	[[maybe_unused]] UITextButtonComponent& component) {

	if (!world.HasComponent<UITextButtonRuntimeComponent>(entity)) {
		world.AddComponent<UITextButtonRuntimeComponent>(entity);
	}
}

void Engine::UITextButtonComponent::OnRemoved(
	ECSWorld& world, const Entity& entity) {

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

	from_json(in, component);
}

void Engine::UITextButtonComponent::SerializeECS(
	[[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const UITextButtonComponent& component, nlohmann::json& out) {

	to_json(out, component);
}

//============================================================================
//	UIButtonComponent serialization
//============================================================================
void Engine::from_json(const nlohmann::json& in, UIImageButtonComponent& component) {

	ReadButton(in, component);
}

void Engine::to_json(nlohmann::json& out, const UIImageButtonComponent& component) {

	WriteButton(out, component);
}

void Engine::from_json(const nlohmann::json& in, UITextButtonComponent& component) {

	ReadButton(in, component);
}

void Engine::to_json(nlohmann::json& out, const UITextButtonComponent& component) {

	WriteButton(out, component);
}
