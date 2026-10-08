#include "ReparentEntitiesCommand.h"

//============================================================================
//	include
//============================================================================
#include "ReparentEntityCommand.h"
#include <Engine/Editor/Core/EditorState.h>

// c++
#include <utility>

Engine::ReparentEntitiesCommand::ReparentEntitiesCommand(std::vector<Entity> targetEntities, UUID newParentStableUUID)
	: targetEntities_(std::move(targetEntities)), newParentStableUUID_(newParentStableUUID) {
}

bool Engine::ReparentEntitiesCommand::Execute(EditorCommandContext& context) {

	if (initialized_) {
		return Redo(context);
	}
	if (targetEntities_.empty()) {
		return false;
	}
	if (!command_) {

		// 失敗後の再実行でも対象を重複させない
		std::vector<std::unique_ptr<IEditorCommand>> commands;
		commands.reserve(targetEntities_.size());
		for (const Entity& entity : targetEntities_) {
			commands.emplace_back(std::make_unique<ReparentEntityCommand>(entity, newParentStableUUID_));
		}
		command_ = std::make_unique<CompositeEditorCommand>(std::move(commands));
	}
	// 途中失敗と例外の取消は共通Commandが行う
	const bool applied = command_->Execute(context);
	initialized_ = applied;
	RestoreSelection(context);
	return applied;
}

void Engine::ReparentEntitiesCommand::Undo(EditorCommandContext& context) {

	// 単体Commandを逆順で戻す
	if (command_) {
		command_->Undo(context);
	}
	RestoreSelection(context);
}

bool Engine::ReparentEntitiesCommand::Redo(EditorCommandContext& context) {

	if (!command_) {
		return false;
	}
	const bool applied = command_->Redo(context);
	RestoreSelection(context);
	return applied;
}

void Engine::ReparentEntitiesCommand::RestoreSelection(EditorCommandContext& context) const {

	if (context.editorState) {
		context.editorState->SetSelectedEntities(targetEntities_);
	}
}
