#include "ReparentEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>

//============================================================================
//	ReparentEntityCommand classMethods
//============================================================================

Engine::ReparentEntityCommand::ReparentEntityCommand(const Entity& targetEntity, UUID newParentStableUUID) :
	initialTarget_(targetEntity), newParentStableUUID_(newParentStableUUID) {
}

bool Engine::ReparentEntityCommand::ApplyParent(EditorCommandContext& context, UUID parentStableUUID) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	// UUIDからエンティティを検索する
	Entity child = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(child)) {
		return false;
	}

	// 新しい親を検索する。UUIDが無効な場合はNullエンティティになる
	Entity newParent = Entity::Null();
	if (parentStableUUID) {

		newParent = world->FindByUUID(parentStableUUID);
		if (!world->IsAlive(newParent)) {
			return false;
		}
	}

	// 親子関係を更新する
	HierarchySystem hierarchySystem;
	hierarchySystem.SetParent(*world, child, newParent);
	if (context.editorState) {

		context.editorState->SelectEntity(child);
	}
	return true;
}

bool Engine::ReparentEntityCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行時のみ現在の親を保存
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		// 対象のUUIDを保存する
		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent<HierarchyComponent>(initialTarget_)) {

			const auto& hierarchy = world->GetComponent<HierarchyComponent>(initialTarget_);
			if (world->IsAlive(hierarchy.parent)) {
				oldParentStableUUID_ = world->GetUUID(hierarchy.parent);
			}
		}

		// 変化がないなら履歴に積まない
		if (oldParentStableUUID_ == newParentStableUUID_) {
			return false;
		}
	}
	return ApplyParent(context, newParentStableUUID_);
}

void Engine::ReparentEntityCommand::Undo(EditorCommandContext& context) {

	ApplyParent(context, oldParentStableUUID_);
}

bool Engine::ReparentEntityCommand::Redo(EditorCommandContext& context) {

	return ApplyParent(context, newParentStableUUID_);
}