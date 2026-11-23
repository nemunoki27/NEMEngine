#include "EditorManager.h"

//============================================================================
//	include
//============================================================================
#include "EditorSelectionOperations.h"
#include <Engine/Editor/Commands/Core/EditorCommandExecution.h>
#include <Engine/Editor/Animation/AnimationPropertyEditCapture.h>

bool Engine::EditorManager::ExecuteEditorCommand(std::unique_ptr<IEditorCommand> command) {

	if (!currentRenderContext_ || !command) {
		return false;
	}

	const bool assetCommand = dynamic_cast<const IEditorAssetCommand*>(command.get()) != nullptr;
	return EditorCommandExecution::Run(
		*currentRenderContext_, editorState_, dirtyState_, [&](EditorCommandContext& commandContext) {
			// Play中は実行Worldへ直接反映しUndoへ積まない
			return currentRenderContext_->isPlaying && !assetCommand ? command->Execute(commandContext)
													: editorState_.commandHistory.Execute(std::move(command), commandContext);
		}, !assetCommand);
}

bool Engine::EditorManager::UndoEditorCommand() {

	if (currentRenderContext_ && currentRenderContext_->activeWorld &&
		AnimationPropertyEditCapture::HasRecorder(*currentRenderContext_->activeWorld))
		return AnimationPropertyEditCapture::RestoreHistory(*currentRenderContext_->activeWorld, false);
	// Play中はEdit Worldの履歴を操作しない
	const bool assetCommand = dynamic_cast<const IEditorAssetCommand*>(editorState_.commandHistory.PeekUndo()) != nullptr;
	if (!currentRenderContext_ || (currentRenderContext_->isPlaying && !assetCommand)) {
		return false;
	}

	return EditorCommandExecution::Run(*currentRenderContext_, editorState_, dirtyState_,
		[&](EditorCommandContext& commandContext) { return editorState_.commandHistory.Undo(commandContext); }, !assetCommand);
}

bool Engine::EditorManager::RedoEditorCommand() {

	if (currentRenderContext_ && currentRenderContext_->activeWorld &&
		AnimationPropertyEditCapture::HasRecorder(*currentRenderContext_->activeWorld))
		return AnimationPropertyEditCapture::RestoreHistory(*currentRenderContext_->activeWorld, true);
	// Play中はEdit Worldの履歴を操作しない
	const bool assetCommand = dynamic_cast<const IEditorAssetCommand*>(editorState_.commandHistory.PeekRedo()) != nullptr;
	if (!currentRenderContext_ || (currentRenderContext_->isPlaying && !assetCommand)) {
		return false;
	}

	return EditorCommandExecution::Run(*currentRenderContext_, editorState_, dirtyState_,
		[&](EditorCommandContext& commandContext) { return editorState_.commandHistory.Redo(commandContext); }, !assetCommand);
}

bool Engine::EditorManager::DuplicateSelection() {

	return EditorSelectionOperations::Duplicate(currentRenderContext_, editorState_, *this);
}

bool Engine::EditorManager::DeleteSelection() {

	return EditorSelectionOperations::Delete(currentRenderContext_, editorState_, *this);
}

bool Engine::EditorManager::CopySelectionToClipboardInternal(const EditorContext& context) {

	return EditorSelectionOperations::Copy(context, editorState_);
}

bool Engine::EditorManager::CopySelectionToClipboard() {

	if (!currentRenderContext_) {
		return false;
	}
	return CopySelectionToClipboardInternal(*currentRenderContext_);
}

bool Engine::EditorManager::PasteClipboard() {

	return EditorSelectionOperations::Paste(currentRenderContext_, editorState_, *this);
}

void Engine::EditorManager::RequestMarkSceneDirty() {

	MarkCurrentSceneDirty();
}

void Engine::EditorManager::ResetSceneEditingState() {

	editorState_.ClearSelection();
	editorState_.commandHistory.Clear();
	requests_.ResetPending();
}

void Engine::EditorManager::MarkCurrentSceneDirty() {

	if (!currentRenderContext_ || currentRenderContext_->isPlaying || currentRenderContext_->isPrefabEditing ||
		!currentRenderContext_->activeSceneAsset) {
		return;
	}
	dirtyState_.MarkDirty(currentRenderContext_->activeSceneAsset, currentRenderContext_->activeSceneInstanceID);
}
