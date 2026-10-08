#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/World/UI/UIRuntimeService.h>

namespace {

	// Player 0のゲーム入力を受け取れるか確認
	bool IsPrimaryGameplayInputAvailable(Engine::Input* input) {

		return input && input->IsGameplayInputAvailable(0) &&
			!Engine::UIRuntimeService::GetInstance().IsGameplayInputBlocked(0);
	}
}

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
		if (!IsPrimaryGameplayInputAvailable(input)) {
			return 0;
		}
		// 指定されたキーが現在押されているか確認
		return input && input->PushKey(static_cast<BYTE>(key)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetKeyDownCallback(int32_t key) {
		if (key < 0 || 255 < key) {
			return 0;
		}
		Input* input = Input::GetInstance();
		if (!IsPrimaryGameplayInputAvailable(input)) {
			return 0;
		}
		// 今フレームでキーが押された瞬間かを確認するトリガー判定
		return input && input->TriggerKey(static_cast<BYTE>(key)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetKeyUpCallback(int32_t key) {
		if (key < 0 || 255 < key) {
			return 0;
		}
		Input* input = Input::GetInstance();
		if (!IsPrimaryGameplayInputAvailable(input)) {
			return 0;
		}
		// 今フレームでキーが離された瞬間かを確認
		return input && input->ReleaseKey(static_cast<BYTE>(key)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetMouseButtonCallback(int32_t button) {
		Input* input = Input::GetInstance();
		if (!IsPrimaryGameplayInputAvailable(input)) {
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
		if (!IsPrimaryGameplayInputAvailable(input)) {
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
		if (!IsPrimaryGameplayInputAvailable(input)) {
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
		return IsPrimaryGameplayInputAvailable(input) ?
			ToManagedVector2(input->GetMouseMoveValue()) : ManagedVector2{};
	}

	float ManagedScriptRuntime::GetMouseWheelCallback() {
		Input* input = Input::GetInstance();
		// ホイールの回転量を取得し上回転で正、下回転で負
		return IsPrimaryGameplayInputAvailable(input) ? input->GetMouseWheel() : 0.0f;
	}

	int32_t ManagedScriptRuntime::GetGamepadButtonCallback(int32_t button) {
		if (button < 0 || static_cast<int32_t>(GamePadButtons::Counts) <= button) {
			return 0;
		}
		Input* input = Input::GetInstance();
		if (!IsPrimaryGameplayInputAvailable(input)) {
			return 0;
		}
		// ゲームパッドのボタン押下状態を確認
		return input && input->PushGamepadButton(static_cast<GamePadButtons>(button)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetGamepadButtonDownCallback(int32_t button) {
		if (button < 0 || static_cast<int32_t>(GamePadButtons::Counts) <= button) {
			return 0;
		}
		Input* input = Input::GetInstance();
		if (!IsPrimaryGameplayInputAvailable(input)) {
			return 0;
		}
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
		// 左スティックの傾きを取得(-1.0 ~ 1.0)
		return IsPrimaryGameplayInputAvailable(input) ?
			ToManagedVector2(input->GetLeftStickVal()) : ManagedVector2{};
	}

	ManagedVector2 ManagedScriptRuntime::GetRightStickCallback() {
		Input* input = Input::GetInstance();
		// 右スティックの傾きを取得(-1.0 ~ 1.0)
		return IsPrimaryGameplayInputAvailable(input) ?
			ToManagedVector2(input->GetRightStickVal()) : ManagedVector2{};
	}

	float ManagedScriptRuntime::GetLeftTriggerCallback() {
		Input* input = Input::GetInstance();
		// 左トリガーの押し込み量を取得(0.0 ~ 1.0)
		return IsPrimaryGameplayInputAvailable(input) ? input->GetLeftTriggerValue() : 0.0f;
	}

	float ManagedScriptRuntime::GetRightTriggerCallback() {
		Input* input = Input::GetInstance();
		// 右トリガーの押し込み量を取得(0.0 ~ 1.0)
		return IsPrimaryGameplayInputAvailable(input) ? input->GetRightTriggerValue() : 0.0f;
	}

	int32_t ManagedScriptRuntime::GetUIBlocksGameplayInputCallback(int32_t playerIndex) {

		return playerIndex >= 0 && UIRuntimeService::GetInstance().IsGameplayInputBlocked(
			static_cast<uint32_t>(playerIndex)) ? 1 : 0;
	}

	//============================================================================
	//	raw Input拡張多gamepadとaxisとtextとfocus
	//============================================================================
	int32_t ManagedScriptRuntime::GetGamepadButtonIndexedCallback(int32_t index, int32_t button) {
		Input* input = Input::GetInstance();
		return (input && input->GamepadButtonByIndex(index, button)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetGamepadButtonDownIndexedCallback(int32_t index, int32_t button) {
		Input* input = Input::GetInstance();
		return (input && input->GamepadButtonDownByIndex(index, button)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetGamepadButtonUpIndexedCallback(int32_t index, int32_t button) {
		Input* input = Input::GetInstance();
		return (input && input->GamepadButtonUpByIndex(index, button)) ? 1 : 0;
	}

	float ManagedScriptRuntime::GetGamepadAxisCallback(int32_t index, int32_t axis) {
		Input* input = Input::GetInstance();
		return input ? input->GamepadAxisByIndex(index, axis) : 0.0f;
	}

	int32_t ManagedScriptRuntime::IsGamepadConnectedIndexedCallback(int32_t index) {
		Input* input = Input::GetInstance();
		return (input && input->GamepadConnectedByIndex(index)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetConnectedGamepadCountCallback() {
		Input* input = Input::GetInstance();
		return input ? input->ConnectedGamepadCount() : 0;
	}

	int32_t ManagedScriptRuntime::GetPlayerGamepadIndexCallback(int32_t playerIndex) {

		Input* input = Input::GetInstance();
		return input && playerIndex >= 0 ? input->GetPlayerGamepadIndex(
			static_cast<uint32_t>(playerIndex)) : -1;
	}

	int32_t ManagedScriptRuntime::GetPlayerKeyboardMouseEnabledCallback(int32_t playerIndex) {

		Input* input = Input::GetInstance();
		return input && playerIndex >= 0 && input->IsPlayerKeyboardMouseEnabled(
			static_cast<uint32_t>(playerIndex)) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::GetPlayerInputAvailableCallback(int32_t playerIndex) {

		Input* input = Input::GetInstance();
		return input && playerIndex >= 0 && input->IsGameplayInputAvailable(
			static_cast<uint32_t>(playerIndex)) &&
			!UIRuntimeService::GetInstance().IsGameplayInputBlocked(
				static_cast<uint32_t>(playerIndex)) ? 1 : 0;
	}

	uint32_t ManagedScriptRuntime::PlayPlayerVibrationCallback(int32_t playerIndex,
		float left, float right, float duration, float attack, float release) {

		Input* input = Input::GetInstance();
		if (!input || playerIndex < 0) {
			return 0;
		}
		return input->PlayVibration(static_cast<uint32_t>(playerIndex), {
			.left = left,
			.right = right,
			.duration = duration,
			.attack = attack,
			.release = release,
			.priority = 0,
			});
	}

	void ManagedScriptRuntime::StopPlayerVibrationCallback(int32_t playerIndex, uint32_t handle) {

		Input* input = Input::GetInstance();
		if (input && playerIndex >= 0) {
			input->StopVibration(static_cast<uint32_t>(playerIndex), handle);
		}
	}

	int32_t ManagedScriptRuntime::GetHasFocusCallback() {
		Input* input = Input::GetInstance();
		// Input未初期化時はフォーカスありとみなす安全側の扱い
		return (!input || input->HasWindowFocus()) ? 1 : 0;
	}

	int32_t ManagedScriptRuntime::CopyTextInputCallback(char* buffer, int32_t capacity) {
		Input* input = Input::GetInstance();
		if (!input) {
			return 0;
		}
		// frame-localのUTF-8テキストをlength-query方式でコピーし固定buffer truncateを避ける
		return CopyStringToBuffer(input->FrameTextInput(), buffer, capacity);
	}

} // Engine
