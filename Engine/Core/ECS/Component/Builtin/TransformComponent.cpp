#include "TransformComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>

//============================================================================
//	TransformComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, TransformComponent& component) {

	component.localPos = Vector3::FromJson(in.value("localPos", nlohmann::json{}));
	component.localRotation = Quaternion::FromJson(in.value("localRotation", nlohmann::json{}));
	component.localScale = Vector3::FromJson(in.value("localScale", nlohmann::json{}));

	// ワールド行列と変更検知フラグはシリアライズされないため、初期化しておく
	component.worldMatrix = Matrix4x4::Identity();
	component.isDirty = true;
}

void Engine::to_json(nlohmann::json& out, const TransformComponent& component) {

	out["localPos"] = component.localPos.ToJson();
	out["localRotation"] = component.localRotation.ToJson();
	out["localScale"] = component.localScale.ToJson();
}

void Engine::MarkTransformSubtreeDirty(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity)) {
		return;
	}

	if (world.HasComponent<Engine::TransformComponent>(entity)) {
		world.GetComponent<Engine::TransformComponent>(entity).isDirty = true;
	}

	if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
		return;
	}
	Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(entity).firstChild;
	while (child.IsValid() && world.IsAlive(child)) {

		MarkTransformSubtreeDirty(world, child);

		if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
			break;
		}
		child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
	}
}