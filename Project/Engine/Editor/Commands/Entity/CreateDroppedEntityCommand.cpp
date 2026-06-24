#include "CreateDroppedEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>

//============================================================================
//	CreateDroppedEntityCommand classMethods
//============================================================================
Engine::CreateDroppedEntityCommand::CreateDroppedEntityCommand(const Entity& createdEntity) :
	createdEntity_(createdEntity) {
}

bool Engine::CreateDroppedEntityCommand::Execute(EditorCommandContext& context) {

	// 既にD&Dで作成済みなので、初回は対象UUIDとスナップショットを控えるだけにする
	if (captured_) {
		return Redo(context);
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !world->IsAlive(createdEntity_)) {
		return false;
	}

	targetStableUUID_ = world->GetUUID(createdEntity_);
	EditorEntitySnapshotUtility::CaptureSubtree(*world, createdEntity_, snapshot_);
	if (snapshot_.IsEmpty()) {
		return false;
	}
	captured_ = true;
	return true;
}

void Engine::CreateDroppedEntityCommand::Undo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return;
	}

	// 破棄前に最新状態をスナップショットへ取り直す、作成後の編集差分もRedoで戻せるようにする
	Entity target = world->FindByUUID(targetStableUUID_);
	if (world->IsAlive(target)) {

		EditorEntitySnapshotUtility::CaptureSubtree(*world, target, snapshot_);
		EditorEntitySnapshotUtility::DestroySubtree(*world, target);
		context.RebuildHierarchyAll();
	}

	// 削除したので選択をクリアする
	if (context.editorState) {
		context.editorState->SelectEntity(Entity::Null());
	}
}

bool Engine::CreateDroppedEntityCommand::Redo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || snapshot_.IsEmpty()) {
		return false;
	}

	// スナップショットからエンティティを復元する
	const std::vector<Entity> restoredEntities = EditorEntitySnapshotUtility::RestoreSubtree(*world, snapshot_);
	EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(context, *world, snapshot_, restoredEntities);
	context.RebuildHierarchyAll();

	// 復元したエンティティを選択する
	if (context.editorState) {
		context.editorState->SelectEntity(world->FindByUUID(targetStableUUID_));
	}
	return true;
}
