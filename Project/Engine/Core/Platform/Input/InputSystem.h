#pragma once

//============================================================================
//	include
//============================================================================
#include "InputHardware.h"
#include "InputViewMapping.h"
#include "InputWindowEvents.h"
#include "InputVibrationPlayer.h"
#include <Engine/Core/Platform/Input/InputTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// directInput
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <XInput.h>
// c++
#include <cmath>
#include <string>
#include <string_view>
#include <cstdint>
#include <array>
#include <cassert>
#include <optional>
#include <source_location>

namespace Engine {

	// front
	class WinApp;

	//============================================================================
	//	Input class
	//	デバイスに応じた入力を管理、キーボード操作、ゲームパッド操作、マウス操作を扱う
	//============================================================================
	class Input {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		void Init(WinApp* winApp);
		void Update();

		//--------- accessor -----------------------------------------------------

		// key
		bool PushKey(BYTE keyNumber, const std::source_location& location = std::source_location::current());
		bool TriggerKey(BYTE keyNumber, const std::source_location& location = std::source_location::current());
		bool ReleaseKey(BYTE keyNumber, const std::source_location& location = std::source_location::current());

		// gamePad
		bool PushGamepadButton(GamePadButtons button, const std::source_location& location = std::source_location::current());
		bool TriggerGamepadButton(GamePadButtons button, const std::source_location& location = std::source_location::current());

		Vector2 GetLeftStickVal() const;
		Vector2 GetRightStickVal() const;

		float GetLeftTriggerValue() const;
		float GetRightTriggerValue() const;

		// mouse
		bool PushMouseLeft(const std::source_location& location = std::source_location::current()) const { return PushMouseButton(0, location); }
		bool PushMouseRight(const std::source_location& location = std::source_location::current()) const { return PushMouseButton(1, location); }
		bool PushMouseCenter(const std::source_location& location = std::source_location::current()) const { return PushMouseButton(2, location); }
		bool PushMouse(MouseButton button, const std::source_location& location = std::source_location::current()) const;

		bool TriggerMouseLeft(const std::source_location& location = std::source_location::current()) const;
		bool TriggerMouseRight(const std::source_location& location = std::source_location::current()) const;
		bool TriggerMouseCenter(const std::source_location& location = std::source_location::current()) const;
		bool TriggerMouse(MouseButton button, const std::source_location& location = std::source_location::current()) const;

		bool ReleaseMouse(MouseButton button, const std::source_location& location = std::source_location::current()) const;

		Vector2 GetMousePos() const;
		Vector2 GetMousePrePos() const;
		Vector2 GetMouseMoveValue() const;
		float GetMouseWheel();

		InputType GetType() const { return inputType_; }

		// deadZone
		void SetDeadZone(float deadZone);
		float GetDeadZone() const { return deadZone_; }

		// 実際の入力操作から入力タイプを更新しマウス範囲制御も行う
		void UpdateInputDevice();

		// マウス移動範囲制御
		void SetMouseRangeControl(bool enabled) { mouseRangeControl_ = enabled; }
		bool GetMouseRangeControl() const { return mouseRangeControl_; }
		void SetMouseArea(const Vector2& pos, const Vector2& size) { mouseAreaPos_ = pos; mouseAreaSize_ = size; }
		Vector2 GetMouseAreaPos() const { return mouseAreaPos_; }
		Vector2 GetMouseAreaSize() const { return mouseAreaSize_; }
		// 範囲制御解除ショートカット、modKeyを押しながらtriggerKeyで解除、共にDIKコード
		void SetMouseReleaseShortcut(int32_t modKey, int32_t triggerKey) { mouseReleaseModKey_ = modKey; mouseReleaseTriggerKey_ = triggerKey; }
		int32_t GetMouseReleaseModKey() const { return mouseReleaseModKey_; }
		int32_t GetMouseReleaseTriggerKey() const { return mouseReleaseTriggerKey_; }

		// UserSettingsの入力デバイス設定を読み書きする
		void LoadConfig();
		void SaveConfig() const;

		// maxStickValue
		float GetMaxStickValue() const { return maxStickValue_; }

		// view
		void SetViewRect(InputViewArea viewArea, const Vector2& dstPos,
			const Vector2& dstSize, const Vector2& srcSize = Vector2::AnyInit(0.0f),
			InputViewCoordinateSpace coordinateSpace = InputViewCoordinateSpace::Client);
		bool HasViewRect(InputViewArea viewArea) const;
		bool IsMouseOnView(InputViewArea viewArea) const;
		std::optional<Vector2> GetMousePosInView(InputViewArea viewArea) const;
		Vector2 GetMouseMoveValueInView(InputViewArea viewArea) const;

		// ゲームパッドの振動
		uint32_t PlayVibration(const InputVibrationParams& params);
		// 指定のID振動の停止
		void StopVibration(uint32_t handle);
		// 全ての振動を停止
		void StopAllVibration();
		// 振動の有効、無効の設定
		void SetVibrationEnabled(bool enabled);
		// ゲームパッドが繋がっているかどうか
		bool IsGamepadConnected() const { return hardware_.GetState().gamepadConnected; }

		//--------- gameplay向け多gamepad / text / focus -------------------------
		// 既存のsingle-gamepad path上の各accessorは変更せず、scripting用に独立のsnapshotを持つ
		// button / axisのindexはC#のGamepadButton / GamepadAxis enumに対応する
		bool GamepadConnectedByIndex(int index) const;
		int ConnectedGamepadCount() const;
		bool GamepadButtonByIndex(int index, int button) const;
		bool GamepadButtonDownByIndex(int index, int button) const;
		bool GamepadButtonUpByIndex(int index, int button) const;
		float GamepadAxisByIndex(int index, int axis) const;
		// このフレームで読める確定テキストでUTF-8のframe-local
		const std::string& FrameTextInput() const { return windowEvents_.FrameTextInput(); }
		// ウィンドウがフォーカスを持っているか
		bool HasWindowFocus() const { return windowEvents_.HasWindowFocus(); }
		// WinAppのWM_CHAR / focusメッセージからmain threadで呼ぶ
		void AppendTextInputUtf16(wchar_t code) { windowEvents_.AppendTextInputUtf16(code); }
		void SetWindowFocus(bool focused) { windowEvents_.SetWindowFocus(focused); }

		// 外部エクスプローラーからのファイルドロップを画面座標で積む
		void PushDroppedFiles(const std::vector<std::string>& paths, const Vector2& screenPoint);
		// 溜めたファイルドロップを取り出して消費する、未着なら何もせずfalse
		bool TakeDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint);
		// 溜めたファイルドロップを消費せず取得する
		bool PeekDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) const;

		// singleton
		static Input* GetInstance();
		// 生存中の入力だけを取得し、新規作成しない
		static Input* TryGetInstance();
		static void Finalize();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		static Input* instance_;

		WinApp* winApp_;
		InputHardware hardware_;
		InputWindowEvents windowEvents_;

		// 入力状態
		InputType inputType_ = InputType::Keyboard;

		// デッドゾーンの閾値
		float deadZone_ = 8000.0f;
		// スティック入力の最大値
		const float maxStickValue_ = 32767.0f;

		// マウス移動範囲制御
		bool mouseRangeControl_ = false;
		bool mouseRangeControlPrev_ = false;
		Vector2 mouseAreaPos_{};
		Vector2 mouseAreaSize_{};
		// 範囲制御解除ショートカットのDIKキー、既定はCtrl+Enter
		int32_t mouseReleaseModKey_ = DIK_LCONTROL;
		int32_t mouseReleaseTriggerKey_ = DIK_RETURN;

		// 描画矩形範囲
		InputViewMapping views_;

		InputVibrationPlayer vibration_;

		//--------- functions ----------------------------------------------------

		// 指定したマウスボタンの現在値を取得
		bool PushMouseButton(size_t index, const std::source_location& location) const;
		// キーボードかマウスの操作を検出
		bool HasKeyboardMouseInput() const;
		// ゲームパッドの操作を検出
		bool HasGamepadInput() const;

		Input() = default;
		~Input() = default;
		Input(const Input&) = delete;
		Input& operator=(const Input&) = delete;
	};
}; // Engine
