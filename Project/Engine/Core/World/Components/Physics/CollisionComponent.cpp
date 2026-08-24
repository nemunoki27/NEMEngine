#include "CollisionComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace {

	// チャンク内設定をjsonへ書き出す
	void SaveSettings(const Engine::CollisionComponent& component, nlohmann::json& out) {

		out["enabled"] = component.enabled;
		out["isStatic"] = component.isStatic;
		out["enablePushback"] = component.enablePushback;
		out["typeMask"] = component.typeMask;
		out["shape"] = component.shape;
	}
}

//============================================================================
//	CollisionComponent classMethods
//============================================================================
void Engine::CollisionComponent::OnAdded(
	ECSWorld& world, const Entity& entity, [[maybe_unused]] CollisionComponent& component) {

	// Edit中も接触表示を更新できるよう非保存状態は両方のWorldへ追加する
	if (!world.HasComponent<CollisionRuntimeStateComponent>(entity)) {
		world.AddComponent<CollisionRuntimeStateComponent>(entity);
	}
}

void Engine::CollisionComponent::OnRemoved(ECSWorld& world, const Entity& entity) {

	// 設定Componentに従属する非保存状態を同時に破棄する
	if (world.HasComponent<CollisionRuntimeStateComponent>(entity)) {
		world.RemoveComponent<CollisionRuntimeStateComponent>(entity);
	}
}

void Engine::CollisionComponent::InitializeStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CollisionComponent& component) {
}

void Engine::CollisionComponent::ReleaseStorage(
	[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity,
	[[maybe_unused]] CollisionComponent& component) {
}

void Engine::CollisionComponent::DeserializeECS(ECSWorld& world, const Entity& entity,
	const nlohmann::json& in, CollisionComponent& component) {

	DeserializeComponent(world, entity, in, component);
}

void Engine::CollisionComponent::SerializeECS(const ECSWorld& world, const Entity& entity,
	const CollisionComponent& component, nlohmann::json& out) {

	SerializeComponent(world, entity, component, out);
}

bool Engine::IsCollisionColliding(const ECSWorld& world, const Entity& entity) {

	const CollisionRuntimeStateComponent* state =
		world.TryGetComponent<CollisionRuntimeStateComponent>(entity);
	return state && state->colliding;
}

void Engine::DeserializeComponent([[maybe_unused]] ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const nlohmann::json& in, CollisionComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.isStatic = in.value("isStatic", component.isStatic);
	component.enablePushback = in.value("enablePushback", component.enablePushback);
	component.typeMask = in.value("typeMask", component.typeMask);
	if (in.contains("shape") && in["shape"].is_object()) {
		component.shape = in["shape"].get<CollisionShape>();
	}
}

void Engine::SerializeComponent([[maybe_unused]] const ECSWorld& world,
	[[maybe_unused]] const Entity& entity,
	const CollisionComponent& component, nlohmann::json& out) {

	SerializeComponentDraft(component, out);
}

void Engine::SerializeComponentDraft(
	const CollisionComponent& component, nlohmann::json& out) {

	SaveSettings(component, out);
}
