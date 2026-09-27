#include "CompositeEditorCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>

// c++
#include <algorithm>
#include <stdexcept>
#include <utility>

//============================================================================
//	CompositeEditorCommand classMethods
//============================================================================
Engine::CompositeEditorCommand::CompositeEditorCommand(
	std::vector<std::unique_ptr<IEditorCommand>> commands) :
	commands_(std::move(commands)) {
}

bool Engine::CompositeEditorCommand::Execute(EditorCommandContext& context) {

	previousSelection_.Capture(context);
	return Apply(context, false);
}

void Engine::CompositeEditorCommand::Undo(EditorCommandContext& context) {

	EditorSelectionSnapshot selection;
	selection.Capture(context);
	try {
		UndoApplied(context, commands_.size());
		previousSelection_.Restore(context);
	} catch (...) {
		selection.Restore(context);
		throw;
	}
}

bool Engine::CompositeEditorCommand::Redo(EditorCommandContext& context) {

	return Apply(context, true);
}

bool Engine::CompositeEditorCommand::Apply(EditorCommandContext& context, bool redo) {

	EditorSelectionSnapshot selection;
	selection.Capture(context);
	size_t appliedCount = 0;
	try {
		for (const auto& command : commands_) {

			if (!command || !(redo ? command->Redo(context) : command->Execute(context))) {
				break;
			}
			++appliedCount;
		}
		if (appliedCount == commands_.size() && appliedCount != 0) {
			if (redo) {
				appliedSelection_.Restore(context);
			} else {
				appliedSelection_.Capture(context);
			}
			return true;
		}
	} catch (...) {
		// 例外でも適用済み部分と選択を戻す
		UndoApplied(context, appliedCount);
		selection.Restore(context);
		throw;
	}
	UndoApplied(context, appliedCount);
	selection.Restore(context);
	return false;
}

const char* Engine::CompositeEditorCommand::GetName() const {

	return "CompositeEditorCommand";
}

void Engine::CompositeEditorCommand::UndoApplied(EditorCommandContext& context, size_t count) {

	const size_t end = (std::min)(count, commands_.size());
	for (size_t index = end; index > 0; --index) {
		try {
			if (commands_[index - 1]) {
				commands_[index - 1]->Undo(context);
			}
		} catch (...) {
			// 取消済み部分を戻し、途中の状態を履歴へ残さない
			for (size_t restored = index; restored < end; ++restored) {
				if (commands_[restored] && !commands_[restored]->Redo(context)) {
					throw std::runtime_error("一括操作の取消失敗後に状態を復元できませんでした");
				}
			}
			throw;
		}
	}
}
