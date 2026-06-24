#include "DeleteEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>

//============================================================================
//	DeleteEntityCommand internal
//============================================================================
namespace {

	// Unity準拠、プレファブ編集中のルートだけ削除不可にする、シーン編集中のインスタンスは削除してよい
	bool IsProtectedPrefabRoot(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::EditorContext* editorContext) {

		// プレファブ編集中でなければ(シーン編集中なら)インスタンスのルートでも削除可能
		if (!editorContext || !editorContext->isPrefabEditing) {
			return false;
		}
		// プレファブのルートかつ階層のトップ(親なし)＝編集中プレファブのルートだけ守る、ネストした子は対象外
		if (!world.HasComponent<Engine::PrefabLinkComponent>(entity) ||
			!world.GetComponent<Engine::PrefabLinkComponent>(entity).isPrefabRoot) {
			return false;
		}
		return !world.HasComponent<Engine::HierarchyComponent>(entity) ||
			!world.IsAlive(world.GetComponent<Engine::HierarchyComponent>(entity).parent);
	}
}

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

		// プレファブインスタンスのルートはシーン上で削除させない
		if (IsProtectedPrefabRoot(*world, initialTarget_, context.editorContext)) {
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
	const std::vector<Entity> restoredEntities = EditorEntitySnapshotUtility::RestoreSubtree(*world, snapshot_);
	EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(context, *world, snapshot_, restoredEntities);
	context.RebuildHierarchyAll();

	// 復元後は対象エンティティを選択する
	if (context.editorState) {

		context.editorState->SelectEntity(world->FindByUUID(targetStableUUID_));
	}
}

bool Engine::DeleteEntityCommand::Redo(EditorCommandContext& context) {

	return DeleteInternal(context);
}
