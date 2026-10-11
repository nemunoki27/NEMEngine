#include "EditorShortcutState.h"

//============================================================================
//	EditorShortcutState classMethods
//============================================================================
void Engine::EditorShortcutState::ProcessMessage(UINT message, WPARAM key, LPARAM flags) {

	if (key != VK_TAB && key != VK_ESCAPE) return;
	if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN && message != WM_KEYUP && message != WM_SYSKEYUP) return;
	const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
	// キーリピートから新しい操作を作らない
	if (down && (flags & (LPARAM{ 1 } << 30)) != 0) return;
	const bool previous = tabDown_ && escapeDown_;
	if (key == VK_TAB) tabDown_ = down;
	else escapeDown_ = down;
	if (!previous && tabDown_ && escapeDown_) ++pendingCount_;
}

bool Engine::EditorShortcutState::Consume() {

	if (pendingCount_ == 0) return false;
	--pendingCount_;
	return true;
}

void Engine::EditorShortcutState::Reset() {

	tabDown_ = false;
	escapeDown_ = false;
	pendingCount_ = 0;
}
