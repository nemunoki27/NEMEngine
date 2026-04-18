#include "AddComponentCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>

//============================================================================
//	AddComponentCommand classMethods
//============================================================================

Engine::AddComponentCommand::AddComponentCommand(const Entity& targetEntity, const  std::string_view& typeName) :
	initialTarget_(targetEntity),
	typeName_(typeName) {
}

bool Engine::AddComponentCommand::ApplyAdd(EditorCommandContext& context) {

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

	// すでに同じコンポーネントがあるなら追加できない
	if (!world->AddComponentByName(target, typeName_)) {
		return false;
	}
	if (context.editorState) {

		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::AddComponentCommand::ApplyRemove(EditorCommandContext& context) {

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

	// コンポーネントがないなら削除できない
	if (!world->RemoveComponentByName(target, typeName_)) {
		return false;
	}
	if (context.editorState) {

		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::AddComponentCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行なら、対象エンティティのUUIDを保存する
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent(initialTarget_, typeName_)) {
			return false;
		}
	}
	return ApplyAdd(context);
}

void Engine::AddComponentCommand::Undo(EditorCommandContext& context) {

	ApplyRemove(context);
}

bool Engine::AddComponentCommand::Redo(EditorCommandContext& context) {

	return ApplyAdd(context);
}