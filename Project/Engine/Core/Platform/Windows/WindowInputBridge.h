#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/WindowFileDrop.h>

namespace Engine::WindowInputBridge {

	// フォーカス状態を入力へ通知する
	void NotifyFocus(HWND hwnd);
	// 破棄するWindowから入力機器を切り離す
	void UnregisterWindow(HWND hwnd);
	// 制御文字を選別して文字入力へ渡す
	void AppendCharacter(WPARAM character);
	// 変換済みのドロップを入力へ渡す
	void PushDrop(const WindowFileDrop& drop);
}
