#include "RenameEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>

//============================================================================
//	RenameEntityCommand classMethods
//============================================================================

Engine::RenameEntityCommand::RenameEntityCommand(const Entity& targetEntity, const std::string_view& newName) :
	initialTarget_(targetEntity),
	newName_(newName) {
}

bool Engine::RenameEntityCommand::ApplyName(EditorCommandContext& context, const std::string& name) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	// UUIDからエンティティを検索
	Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	// 名前コンポーネントがなければ追加
	if (!world->HasComponent<NameComponent>(target)) {

		world->AddComponent<NameComponent>(target);
	}

	// 名前を変更
	auto& nameComponent = world->GetComponent<NameComponent>(target);
	nameComponent.name = name;
	if (context.editorState) {

		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::RenameEntityCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行時は対象エンティティのUUIDと元の名前を保存する
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent<NameComponent>(initialTarget_)) {

			oldName_ = world->GetComponent<NameComponent>(initialTarget_).name;
		} else {

			oldName_ = "Entity";
		}
		if (oldName_ == newName_) {
			return false;
		}
	}
	return ApplyName(context, newName_);
}

void Engine::RenameEntityCommand::Undo(EditorCommandContext& context) {

	ApplyName(context, oldName_);
}

bool Engine::RenameEntityCommand::Redo(EditorCommandContext& context) {

	return ApplyName(context, newName_);
}