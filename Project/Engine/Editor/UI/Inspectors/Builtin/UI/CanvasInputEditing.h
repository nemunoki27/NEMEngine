#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputTypes.h>

// c++
#include <vector>

namespace Engine {

	struct ValueEditResult;

	// Canvasの入力コードを編集する
	namespace CanvasInputEditing {

		// キーの追加と削除を表示する
		ValueEditResult DrawKeyboardBindings(const char* label, std::vector<KeyDIKCode>& bindings, KeyDIKCode preferred);
		// ボタンの追加と削除を表示する
		ValueEditResult DrawGamepadBindings(const char* label, std::vector<GamePadButtons>& bindings, GamePadButtons preferred);
	} // CanvasInputEditing
} // Engine
