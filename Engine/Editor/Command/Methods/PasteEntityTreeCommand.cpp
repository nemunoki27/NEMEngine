#include "PasteEntityTreeCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Editor/EditorState.h>
#include <Engine/Editor/Command/EditorEntityDuplicateUtility.h>

//============================================================================
//	PasteEntityTreeCommand classMethods
//============================================================================

namespace {

	// スナップショットからルートの名前を読み取る
	std::string ReadRootNameFromSnapshot(const Engine::EditorEntityTreeSnapshot& snapshot) {

		if (snapshot.IsEmpty()) {
			return "Entity";
		}
		const auto& root = snapshot.entities.front();
		if (!root.components.contains("Name")) {
			return "Entity";
		}
		return root.components["Name"].value("name", "Entity");
	}
}

Engine::PasteEntityTreeCommand::PasteEntityTreeCommand(const EditorEntityTreeSnapshot& sourceSnapshot,
	UUID parentStableUUID) :sourceSnapshot_(sourceSnapshot), parentStableUUID_(parentStableUUID) {
}

bool Engine::PasteEntityTreeCommand::PasteInternal(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || preparedSnapshot_.IsEmpty()) {
		return false;
	}

	// 複製用スナップショットからエンティティを生成する
	Entity pastedRoot = EditorEntityDuplicateUtility::InstantiatePreparedSnapshot(*world, preparedSnapshot_, parentStableUUID_);

	if (!world->IsAlive(pastedRoot)) {
		return false;
	}
	if (context.editorState) {

		context.editorState->SelectEntity(pastedRoot);
	}
	return true;
}

bool Engine::PasteEntityTreeCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || sourceSnapshot_.IsEmpty()) {
		return false;
	}

	// 初回実行時のみペーストで生成される実体を確定する
	if (preparedSnapshot_.IsEmpty()) {

		// 貼り付け元のエンティティツリーのルートの名前からEntity_N形式の一意名を作る
		const std::string duplicatedName =
			EditorEntityDuplicateUtility::MakeUniqueDuplicatedName(*world, ReadRootNameFromSnapshot(sourceSnapshot_));

		// 複製用スナップショットの構築
		EditorEntityDuplicateUtility::BuildDuplicateSnapshot(sourceSnapshot_, duplicatedName, preparedSnapshot_);

		// コピー元が壊れていて sceneInstanceID / sourceAsset が空でも、
		// 貼り付け先シーンの情報で補完する
		EditorEntitySnapshotUtility::FillMissingOwnerRuntimeState(context, *world, parentStableUUID_, preparedSnapshot_);

		createdRootStableUUID_ = preparedSnapshot_.rootStableUUID;
	}
	return PasteInternal(context);
}

void Engine::PasteEntityTreeCommand::Undo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !createdRootStableUUID_) {
		return;
	}

	// UUIDからエンティティを検索して存在するなら削除する
	Entity pastedRoot = world->FindByUUID(createdRootStableUUID_);
	if (world->IsAlive(pastedRoot)) {

		EditorEntitySnapshotUtility::DestroySubtree(*world, pastedRoot);
		context.RebuildHierarchyAll();
	}
	// エディタ状態の選択をクリアする
	if (context.editorState) {

		Entity parent = world->FindByUUID(parentStableUUID_);
		context.editorState->SelectEntity(world->IsAlive(parent) ? parent : Entity::Null());
	}
}

bool Engine::PasteEntityTreeCommand::Redo(EditorCommandContext& context) {

	return PasteInternal(context);
}