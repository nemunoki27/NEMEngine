#include "DuplicateEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Editor/Command/EditorEntityDuplicateUtility.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>

//============================================================================
//	DuplicateEntityCommand classMethods
//============================================================================

namespace {

	// エンティティの親のUUIDを返す
	Engine::UUID GetParentStableUUID(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return Engine::UUID{};
		}

		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		if (!world.IsAlive(hierarchy.parent)) {
			return Engine::UUID{};
		}
		return world.GetUUID(hierarchy.parent);
	}
	// エンティティの名前を返す
	std::string GetEntityName(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (world.HasComponent<Engine::NameComponent>(entity)) {
			return world.GetComponent<Engine::NameComponent>(entity).name;
		}
		return "Entity";
	}
}

Engine::DuplicateEntityCommand::DuplicateEntityCommand(const Entity& targetEntity) :
	initialTarget_(targetEntity) {
}

bool Engine::DuplicateEntityCommand::DuplicateInternal(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || preparedSnapshot_.IsEmpty()) {
		return false;
	}

	// 複製用スナップショットからエンティティを生成する
	Entity duplicatedRoot = EditorEntityDuplicateUtility::InstantiatePreparedSnapshot(
		*world, preparedSnapshot_, sourceParentStableUUID_);

	if (!world->IsAlive(duplicatedRoot)) {
		return false;
	}
	if (context.editorState) {

		context.editorState->SelectEntity(duplicatedRoot);
	}
	return true;
}

bool Engine::DuplicateEntityCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行時のみ複製用スナップショットを生成する
	if (preparedSnapshot_.IsEmpty()) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		// 複製元のエンティティのUUIDと親のUUIDを記録しておく
		sourceStableUUID_ = world->GetUUID(initialTarget_);
		sourceParentStableUUID_ = GetParentStableUUID(*world, initialTarget_);

		// スナップショットの記録
		EditorEntityTreeSnapshot sourceSnapshot{};
		EditorEntitySnapshotUtility::CaptureSubtree(*world, initialTarget_, sourceSnapshot);
		if (sourceSnapshot.IsEmpty()) {
			return false;
		}

		// 複製エンティティの名前
		const std::string duplicatedName = EditorEntityDuplicateUtility::MakeUniqueDuplicatedName(*world, GetEntityName(*world, initialTarget_));
		// 複製用スナップショットの構築
		EditorEntityDuplicateUtility::BuildDuplicateSnapshot(sourceSnapshot, duplicatedName, preparedSnapshot_);
		// 既存の壊れたエンティティでも複製できるように補完
		EditorEntitySnapshotUtility::FillMissingOwnerRuntimeState(
			context, *world, sourceParentStableUUID_, preparedSnapshot_);
		createdRootStableUUID_ = preparedSnapshot_.rootStableUUID;
	}
	return DuplicateInternal(context);
}

void Engine::DuplicateEntityCommand::Undo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !createdRootStableUUID_) {
		return;
	}

	// UUIDから複製してできたルートエンティティを検索
	Entity duplicatedRoot = world->FindByUUID(createdRootStableUUID_);
	if (world->IsAlive(duplicatedRoot)) {

		EditorEntitySnapshotUtility::DestroySubtree(*world, duplicatedRoot);
		context.RebuildHierarchyAll();
	}

	// 元のエンティティを選択状態に戻す
	if (context.editorState) {

		Entity source = world->FindByUUID(sourceStableUUID_);
		context.editorState->SelectEntity(world->IsAlive(source) ? source : Entity::Null());
	}
}

bool Engine::DuplicateEntityCommand::Redo(EditorCommandContext& context) {

	return DuplicateInternal(context);
}