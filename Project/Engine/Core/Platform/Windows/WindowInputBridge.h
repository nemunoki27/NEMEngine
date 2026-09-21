#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/WindowFileDrop.h>

namespace Engine::WindowInputBridge {

	// フォーカス状態を入力へ通知する
	void NotifyFocus(bool focused);
	// 制御文字を選別して文字入力へ渡す
	void AppendCharacter(WPARAM character);
	// 変換済みのドロップを入力へ渡す
	void PushDrop(const WindowFileDrop& drop);
}
