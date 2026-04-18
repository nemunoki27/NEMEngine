#include "DeleteEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>

//============================================================================
//	DeleteEntityCommand classMethods
//============================================================================

Engine::DeleteEntityCommand::DeleteEntityCommand(const Entity& targetEntity) :
	initialTarget_(targetEntity) {
}

bool Engine::DeleteEntityCommand::DeleteInternal(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	// UUIDからエンティティを検索する
	Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	// エンティティを削除する
	EditorEntitySnapshotUtility::DestroySubtree(*world, target);
	context.RebuildHierarchyAll();

	// 削除後は親を選択する、親が存在しない場合は選択をクリアする
	if (context.editorState) {

		Entity parent = world->FindByUUID(parentStableUUIDBeforeDelete_);
		context.editorState->SelectEntity(world->IsAlive(parent) ? parent : Entity::Null());
	}
	return true;
}

bool Engine::DeleteEntityCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行時だけ対象情報とスナップショットを確定する
	if (!targetStableUUID_) {

		// 対象エンティティが存在するか
		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		// UUIDを取得する
		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent<HierarchyComponent>(initialTarget_)) {

			const auto& hierarchy = world->GetComponent<HierarchyComponent>(initialTarget_);
			if (world->IsAlive(hierarchy.parent)) {

				parentStableUUIDBeforeDelete_ = world->GetUUID(hierarchy.parent);
			}
		}
		// スナップショットを取得する
		EditorEntitySnapshotUtility::CaptureSubtree(*world, initialTarget_, snapshot_);
		if (snapshot_.IsEmpty()) {
			return false;
		}
	}
	return DeleteInternal(context);
}

void Engine::DeleteEntityCommand::Undo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || snapshot_.IsEmpty()) {
		return;
	}

	// スナップショットからエンティティを復元する
	EditorEntitySnapshotUtility::RestoreSubtree(*world, snapshot_);
	context.RebuildHierarchyAll();

	// 復元後は対象エンティティを選択する
	if (context.editorState) {

		context.editorState->SelectEntity(world->FindByUUID(targetStableUUID_));
	}
}

bool Engine::DeleteEntityCommand::Redo(EditorCommandContext& context) {

	return DeleteInternal(context);
}