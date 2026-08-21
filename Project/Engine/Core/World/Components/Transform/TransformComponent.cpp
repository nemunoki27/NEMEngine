#include "TransformComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>

//============================================================================
//	TransformComponent classMethods
//============================================================================

void Engine::from_json(const nlohmann::json& in, TransformComponent& component) {

	component.localPos = Vector3::FromJson(in.value("localPos", nlohmann::json{}));
	component.localRotation = Quaternion::FromJson(in.value("localRotation", nlohmann::json{}));
	component.localScale = Vector3::FromJson(in.value("localScale", nlohmann::json{}));
	component.dimension = static_cast<Dimension>(
		in.value("dimension", static_cast<int>(component.dimension)));

	// 親追従の継承設定、未保存の既存シーンは従来通り両方継承する
	component.ignoreParentScale = in.value("ignoreParentScale", false);
	component.ignoreParentRotation = in.value("ignoreParentRotation", false);

	// ワールド行列と変更検知フラグはシリアライズされないため、初期化しておく
	component.worldMatrix = Matrix4x4::Identity();
	component.isDirty = true;
}

void Engine::to_json(nlohmann::json& out, const TransformComponent& component) {

	out["localPos"] = component.localPos.ToJson();
	out["localRotation"] = component.localRotation.ToJson();
	out["localScale"] = component.localScale.ToJson();
	out["dimension"] = static_cast<int>(component.dimension);

	// 既定値のときは出力を省いてシーンJSONを汚さない
	if (component.ignoreParentScale) {
		out["ignoreParentScale"] = component.ignoreParentScale;
	}
	if (component.ignoreParentRotation) {
		out["ignoreParentRotation"] = component.ignoreParentRotation;
	}
}

void Engine::MarkTransformSubtreeDirty(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity)) {
		return;
	}

	std::vector<Entity> stack{ entity };
	while (!stack.empty()) {

		const Entity current = stack.back();
		stack.pop_back();
		if (!world.IsAlive(current)) {
			continue;
		}
		if (world.HasComponent<TransformComponent>(current)) {
			world.GetComponent<TransformComponent>(current).isDirty = true;
		}
		const HierarchyComponent* hierarchy =
			world.TryGetComponent<HierarchyComponent>(current);
		if (!hierarchy) {
			continue;
		}
		Entity child = hierarchy->firstChild;
		while (child.IsValid() && world.IsAlive(child)) {

			stack.emplace_back(child);
			const HierarchyComponent* childHierarchy =
				world.TryGetComponent<HierarchyComponent>(child);
			if (!childHierarchy) {
				break;
			}
			child = childHierarchy->nextSibling;
		}
	}
	// 変更通知は部分木ごとに1回だけ発行しTransformSystemへルートを渡す
	world.MarkComponentModified<TransformComponent>(entity);
}
