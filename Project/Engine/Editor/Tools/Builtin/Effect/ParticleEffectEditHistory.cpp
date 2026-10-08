#include "ParticleEffectEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Command/SnapshotCommand.h>
#include <Engine/Core/Rendering/Particle/ParticleEffectEditBridge.h>

void Engine::ParticleEffectEditSession::UpdateEditing(bool changed, bool itemActive) {

	if (!loaded_) return;
	if (changed) {
		// ドラッグ開始前の値を1操作のUndo基準にする
		if (!pendingBefore_) pendingBefore_ = committedDraft_;
		dirty_ = ToJson(draft_) != ToJson(savedDraft_);
		ApplyToRuntime();
	}
	if (!itemActive) FinishEditing();
}

void Engine::ParticleEffectEditSession::FinishEditing() {

	if (!pendingBefore_) return;
	if (ToJson(*pendingBefore_) != ToJson(draft_)) {
		auto command = std::make_unique<SnapshotCommand<ParticleEffectAsset>>(
			*pendingBefore_, draft_, "Effect編集");
		history_.Execute(std::move(command), draft_);
	}
	committedDraft_ = draft_;
	pendingBefore_.reset();
}

bool Engine::ParticleEffectEditSession::Undo() {

	FinishEditing();
	if (!history_.Undo(draft_)) return false;
	// Undoで破棄したModuleの編集参照を残さない
	groupEditorStates_.clear();
	committedDraft_ = draft_;
	dirty_ = ToJson(draft_) != ToJson(savedDraft_);
	ApplyToRuntime();
	return true;
}

bool Engine::ParticleEffectEditSession::Redo() {

	FinishEditing();
	if (!history_.Redo(draft_)) return false;
	groupEditorStates_.clear();
	committedDraft_ = draft_;
	dirty_ = ToJson(draft_) != ToJson(savedDraft_);
	ApplyToRuntime();
	return true;
}

void Engine::ParticleEffectEditSession::Discard() {

	if (!loaded_) return;
	// 保存済みの値へ戻し、Sceneの未保存上書きも解除する
	ParticleEffectAsset replacement = savedDraft_;
	draft_ = std::move(replacement);
	committedDraft_ = draft_;
	pendingBefore_.reset();
	history_.Clear();
	groupEditorStates_.clear();
	dirty_ = false;
	ParticleEffectEditBridge::GetInstance().Remove(editingID_);
}
