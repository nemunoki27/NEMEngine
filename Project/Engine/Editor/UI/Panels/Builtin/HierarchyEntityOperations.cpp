#include "HierarchyEntityOperations.h"

#include <Engine/Editor/Commands/Entity/EditorHierarchyPolicy.h>
#include <Engine/Editor/Commands/Entity/EntityPropertyCommands.h>
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

#include <algorithm>
#include <unordered_set>

bool Engine::HierarchyEntityOperations::IsRootEntity(ECSWorld& world, const Entity& entity) {

	return HierarchyUtility::IsRoot(world, entity);
}

Engine::Entity Engine::HierarchyEntityOperations::GetParentEntity(ECSWorld& world, const Entity& entity) {

	return HierarchyUtility::GetParent(world, entity);
}

bool Engine::HierarchyEntityOperations::CanReparent(
	const EditorPanelContext& context, ECSWorld& world, const Entity& child, const Entity& newParent) {

	return (world.IsAlive(newParent) || !newParent.IsValid()) &&
		EditorHierarchyPolicy::CanSetParent(context.editorContext, world, child, newParent) &&
		(HierarchyUtility::GetParent(world, child) != newParent || world.HasComponent<JointAttachmentComponent>(child));
}

bool Engine::HierarchyEntityOperations::CanReorder(
	const EditorPanelContext& context, ECSWorld& world, const Entity& child, const Entity& anchor) {

	return EditorHierarchyPolicy::CanReorder(context.editorContext, world, child, anchor);
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
	const EditorPanelContext& context, ECSWorld& world, const ImGuiPayload* payload) {

	const Entity dragged = ResolveDraggedEntity(world, payload);
	if (!world.IsAlive(dragged)) {
		return {};
	}

	std::vector<Entity> candidates{dragged};
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

bool Engine::HierarchyEntityOperations::SetEntityActiveFromHierarchy(
	const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool active) {

	if (!world.IsAlive(entity)) {
		return false;
	}

	// 選択対象をコピーして通知中の変更から切り離す
	std::vector<Entity> targets{entity};
	if (context.editorState && context.editorState->IsEntitySelected(entity)) {
		targets = context.editorState->GetSelectedEntities();
	}
	if (context.IsPlaying()) {

		// 実行Worldだけを変更して履歴には残さない
		bool changed = false;
		for (const Entity& target : targets) {
			changed = SceneObjectUtility::SetActiveSelf(world, target, active) || changed;
		}
		return changed;
	}
	if (!context.CanEditScene() || !context.host) {
		return false;
	}

	// 値が変わるEntityだけを一回のUndoへまとめる
	std::vector<std::unique_ptr<IEditorCommand>> commands;
	commands.reserve(targets.size());
	std::unordered_set<UUID> seen;
	for (const Entity& target : targets) {

		const auto* object = world.TryGetComponent<SceneObjectComponent>(target);
		if (!object || object->activeSelf == active || !seen.insert(world.GetUUID(target)).second) {
			continue;
		}
		commands.emplace_back(std::make_unique<SetEntityActiveCommand>(target, active));
	}
	return !commands.empty() &&
		   context.host->ExecuteEditorCommand(std::make_unique<CompositeEditorCommand>(std::move(commands), true));
}
