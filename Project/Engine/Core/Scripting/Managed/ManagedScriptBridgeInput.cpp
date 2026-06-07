#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>

namespace Engine {

	//============================================================================
	//	Input Callbacks
	//	C#側のInputクラスから呼び出されるネイティブ実装
	//============================================================================

	int32_t ManagedScriptRuntime::GetKeyCallback(int32_t key) {
		// キーコードの範囲チェック
		if (key < 0 || 255 < key) {
			return 0;
		}
		Input* input = Input::GetInstance();
		// 指定されたキーが現在押されているか確認
		return input && input->PushKey(static_cast<BYTE>(key)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetKeyDownCallback(int32_t key) {
		if (key < 0 || 255 < key) {
			return 0;
		}
		Input* input = Input::GetInstance();
		// 今フレームでキーが押された瞬間かを確認（トリガー判定）
		return input && input->TriggerKey(static_cast<BYTE>(key)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetKeyUpCallback(int32_t key) {
		if (key < 0 || 255 < key) {
			return 0;
		}
		Input* input = Input::GetInstance();
		// 今フレームでキーが離された瞬間かを確認
		return input && input->ReleaseKey(static_cast<BYTE>(key)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetMouseButtonCallback(int32_t button) {
		Input* input = Input::GetInstance();
		if (!input) {
			return 0;
		}

		// マウスボタン番号をエンジン内部の判定に振り分け
		switch (button) {
		case 0: return input->PushMouseLeft() ? 1 : 0;
		case 1: return input->PushMouseRight() ? 1 : 0;
		case 2: return input->PushMouseCenter() ? 1 : 0;
		default: return 0;
		}
	}

	int32_t ManagedScriptRuntime::GetMouseButtonDownCallback(int32_t button) {
		Input* input = Input::GetInstance();
		if (!input) {
			return 0;
		}

		switch (button) {
		case 0: return input->TriggerMouseLeft() ? 1 : 0;
		case 1: return input->TriggerMouseRight() ? 1 : 0;
		case 2: return input->TriggerMouseCenter() ? 1 : 0;
		default: return 0;
		}
	}

	int32_t ManagedScriptRuntime::GetMouseButtonUpCallback(int32_t button) {
		Input* input = Input::GetInstance();
		if (!input) {
			return 0;
		}

		switch (button) {
		case 0: return input->ReleaseMouse(MouseButton::Left) ? 1 : 0;
		case 1: return input->ReleaseMouse(MouseButton::Right) ? 1 : 0;
		case 2: return input->ReleaseMouse(MouseButton::Center) ? 1 : 0;
		default: return 0;
		}
	}

	ManagedVector2 ManagedScriptRuntime::GetMousePositionCallback() {
		Input* input = Input::GetInstance();
		// スクリーン空間でのマウス座標を取得
		return input ? ToManagedVector2(input->GetMousePos()) : ManagedVector2{};
	}

	ManagedVector2 ManagedScriptRuntime::GetMouseDeltaCallback() {
		Input* input = Input::GetInstance();
		// 前フレームからのマウス移動量を取得
		return input ? ToManagedVector2(input->GetMouseMoveValue()) : ManagedVector2{};
	}

	float ManagedScriptRuntime::GetMouseWheelCallback() {
		Input* input = Input::GetInstance();
		// ホイールの回転量を取得（上回転で正、下回転で負）
		return input ? input->GetMouseWheel() : 0.0f;
	}

	int32_t ManagedScriptRuntime::GetGamepadButtonCallback(int32_t button) {
		if (button < 0 || static_cast<int32_t>(GamePadButtons::Counts) <= button) {
			return 0;
		}
		Input* input = Input::GetInstance();
		// ゲームパッドのボタン押下状態を確認
		return input && input->PushGamepadButton(static_cast<GamePadButtons>(button)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetGamepadButtonDownCallback(int32_t button) {
		if (button < 0 || static_cast<int32_t>(GamePadButtons::Counts) <= button) {
			return 0;
		}
		Input* input = Input::GetInstance();
		// ゲームパッドのボタン押下トリガーを確認
		return input && input->TriggerGamepadButton(static_cast<GamePadButtons>(button)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::IsGamepadConnectedCallback() {
		Input* input = Input::GetInstance();
		// 有効なゲームパッドが接続されているか確認
		return input && input->IsGamepadConnected() ? 1 : 0;
	}

	ManagedVector2 ManagedScriptRuntime::GetLeftStickCallback() {
		Input* input = Input::GetInstance();
		// 左スティックの傾きを取得 (-1.0 ~ 1.0)
		return input ? ToManagedVector2(input->GetLeftStickVal()) : ManagedVector2{};
	}

	ManagedVector2 ManagedScriptRuntime::GetRightStickCallback() {
		Input* input = Input::GetInstance();
		// 右スティックの傾きを取得 (-1.0 ~ 1.0)
		return input ? ToManagedVector2(input->GetRightStickVal()) : ManagedVector2{};
	}

	float ManagedScriptRuntime::GetLeftTriggerCallback() {
		Input* input = Input::GetInstance();
		// 左トリガーの押し込み量を取得 (0.0 ~ 1.0)
		return input ? input->GetLeftTriggerValue() : 0.0f;
	}

	float ManagedScriptRuntime::GetRightTriggerCallback() {
		Input* input = Input::GetInstance();
		// 右トリガーの押し込み量を取得 (0.0 ~ 1.0)
		return input ? input->GetRightTriggerValue() : 0.0f;
	}

} // Engine
