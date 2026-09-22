#include "HierarchyEntityOperations.h"

#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>

#include <algorithm>

bool Engine::HierarchyEntityOperations::IsRootEntity(ECSWorld& world, const Entity& entity) {

	if (!world.HasComponent<HierarchyComponent>(entity)) {
		return true;
	}

	const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
	return !world.IsAlive(hierarchy.parent);
}

Engine::Entity Engine::HierarchyEntityOperations::GetParentEntity(ECSWorld& world, const Entity& entity) {

	if (!world.IsAlive(entity) || !world.HasComponent<HierarchyComponent>(entity)) {
		return Entity::Null();
	}
	const auto& hierarchy = world.GetComponent<HierarchyComponent>(entity);
	return world.IsAlive(hierarchy.parent) ? hierarchy.parent : Entity::Null();
}

bool Engine::HierarchyEntityOperations::CanReparent(const EditorPanelContext& context, ECSWorld& world,
	const Entity& child, const Entity& newParent) {

	// どちらも有効なエンティティでなければならない
	if (!world.IsAlive(child) || !world.IsAlive(newParent)) {
		return false;
	}
	if (child == newParent) {
		return false;
	}
	if (!PrefabInstanceEditUtility::CanChangeParent(context.editorContext, world, child, newParent)) {
		return false;
	}
	if (world.HasComponent<SceneObjectComponent>(child) &&
		world.HasComponent<SceneObjectComponent>(newParent) &&
		world.GetComponent<SceneObjectComponent>(child).sceneInstanceID !=
		world.GetComponent<SceneObjectComponent>(newParent).sceneInstanceID) {
		return false;
	}

	// 自分自身の子孫の下には入れられない
	Entity cursor = newParent;
	while (world.IsAlive(cursor)) {

		if (cursor == child) {
			return false;
		}
		if (!world.HasComponent<HierarchyComponent>(cursor)) {
			break;
		}
		// 親をたどる
		cursor = world.GetComponent<HierarchyComponent>(cursor).parent;
	}

	// すでにその親なら意味ないので処理しない
	if (world.HasComponent<HierarchyComponent>(child)) {
		const auto& hierarchy = world.GetComponent<HierarchyComponent>(child);
		if (hierarchy.parent == newParent) {
			return false;
		}
	}
	return true;
}

bool Engine::HierarchyEntityOperations::CanReorder(const EditorPanelContext& context, ECSWorld& world,
	const Entity& child, const Entity& anchor) {

	if (!world.IsAlive(child) || !world.IsAlive(anchor) || child == anchor) {
		return false;
	}
	if (!PrefabInstanceEditUtility::CanChangeSiblingOrder(context.editorContext, world, child, anchor)) {
		return false;
	}
	if (world.HasComponent<SceneObjectComponent>(child) &&
		world.HasComponent<SceneObjectComponent>(anchor) &&
		world.GetComponent<SceneObjectComponent>(child).sceneInstanceID !=
		world.GetComponent<SceneObjectComponent>(anchor).sceneInstanceID) {
		return false;
	}
	return GetParentEntity(world, child) == GetParentEntity(world, anchor);
}

Engine::Entity Engine::HierarchyEntityOperations::ResolveDraggedEntity(ECSWorld& world, const ImGuiPayload* payload) {

	if (!payload || payload->DataSize != sizeof(UUID)) {
		return Entity::Null();
	}

	// ペイロードからUUIDを取得してエンティティを検索
	const UUID stableUUID = *static_cast<const UUID*>(payload->Data);
	return world.FindByUUID(stableUUID);
}

std::vector<Engine::Entity> Engine::HierarchyEntityOperations::ResolveDraggedEntities(
	const EditorPanelContext& context, ECSWorld& world,
	const ImGuiPayload* payload) {

	const Entity dragged = ResolveDraggedEntity(world, payload);
	if (!world.IsAlive(dragged)) {
		return {};
	}

	std::vector<Entity> candidates{ dragged };
	if (context.editorState && context.editorState->IsEntitySelected(dragged)) {
		candidates = context.editorState->GetSelectedEntities();
	}

	std::vector<Entity> roots;
	roots.reserve(candidates.size());
	for (const Entity& candidate : candidates) {
		if (!world.IsAlive(candidate)) {
			continue;
		}

		bool hasSelectedAncestor = false;
		Entity parent = GetParentEntity(world, candidate);
		while (world.IsAlive(parent)) {
			if (std::find(candidates.begin(), candidates.end(), parent) != candidates.end()) {
				hasSelectedAncestor = true;
				break;
			}
			parent = GetParentEntity(world, parent);
		}
		if (!hasSelectedAncestor) {
			roots.emplace_back(candidate);
		}
	}
	return roots;
}
