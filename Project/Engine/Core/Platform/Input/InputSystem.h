#pragma once

//============================================================================
//	include
//============================================================================
#include "InputDeviceConfiguration.h"
#include "InputHardware.h"
#include "InputViewMapping.h"
#include "InputWindowEvents.h"
#include "InputVibrationPlayer.h"
#include <Engine/Core/Platform/Input/InputTypes.h>
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <string>
#include <cstdint>
#include <array>
#include <optional>
#include <source_location>
#include <vector>

// directInput
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <XInput.h>

namespace Engine {

	// 前方宣言
	class WinApp;

	//============================================================================
	//	Input class
	//	入力機器とPlayerの操作状態を管理する
	//============================================================================
	class Input {
	public:
		static constexpr uint32_t kMaxPlayers = InputDeviceConfiguration::kMaxPlayers;
		//============================================================================
		//	public Methods
		//============================================================================

		void Init(WinApp* winApp);
		void Update();

		//--------- accessor -----------------------------------------------------

		// キーボード
		bool PushKey(BYTE keyNumber, const std::source_location& location = std::source_location::current());
		bool TriggerKey(BYTE keyNumber, const std::source_location& location = std::source_location::current());
		bool ReleaseKey(BYTE keyNumber, const std::source_location& location = std::source_location::current());

		// ゲームパッド
		bool PushGamepadButton(GamePadButtons button, const std::source_location& location = std::source_location::current());
		bool TriggerGamepadButton(
			GamePadButtons button, const std::source_location& location = std::source_location::current());

		Vector2 GetLeftStickVal() const;
		Vector2 GetRightStickVal() const;

		float GetLeftTriggerValue() const;
		float GetRightTriggerValue() const;

		// マウス
		bool PushMouseLeft(const std::source_location& location = std::source_location::current()) const;
		bool PushMouseRight(const std::source_location& location = std::source_location::current()) const;
		bool PushMouseCenter(const std::source_location& location = std::source_location::current()) const;
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

		// デッドゾーン
		void SetDeadZone(float deadZone);
		float GetDeadZone() const { return configuration_.deadZone; }

		// 実際の入力操作から入力タイプを更新しマウス範囲制御も行う
		void UpdateInputDevice();

		// マウス移動範囲制御
		void SetMouseRangeControl(bool enabled) { configuration_.mouseRangeControl = enabled; }
		bool GetMouseRangeControl() const { return configuration_.mouseRangeControl; }
		void SetMouseArea(const Vector2& pos, const Vector2& size);
		Vector2 GetMouseAreaPos() const { return configuration_.mouseAreaPos; }
		Vector2 GetMouseAreaSize() const { return configuration_.mouseAreaSize; }
		// 範囲制御を解除するキーの組合せ
		void SetMouseReleaseShortcut(int32_t modKey, int32_t triggerKey);
		int32_t GetMouseReleaseModKey() const { return configuration_.mouseReleaseModKey; }
		int32_t GetMouseReleaseTriggerKey() const { return configuration_.mouseReleaseTriggerKey; }

		// UserSettingsの入力デバイス設定を読み書きする
		void LoadConfig();
		void SaveConfig() const;

		// スティックの最大値
		float GetMaxStickValue() const { return InputDeviceConfiguration::kMaxStickValue; }

		// 表示領域
		void SetViewRect(InputViewArea viewArea, const Vector2& dstPos, const Vector2& dstSize,
			const Vector2& srcSize = Vector2::AnyInit(0.0f),
			InputViewCoordinateSpace coordinateSpace = InputViewCoordinateSpace::Client);
		bool HasViewRect(InputViewArea viewArea) const;
		bool IsMouseOnView(InputViewArea viewArea) const;
		std::optional<Vector2> GetMousePosInView(InputViewArea viewArea) const;
		Vector2 GetMouseMoveValueInView(InputViewArea viewArea) const;

		// ゲームパッドの振動
		uint32_t PlayVibration(const InputVibrationParams& params);
		uint32_t PlayVibration(uint32_t playerIndex, const InputVibrationParams& params);
		// 指定のID振動の停止
		void StopVibration(uint32_t handle);
		void StopVibration(uint32_t playerIndex, uint32_t handle);
		// 全ての振動を停止
		void StopAllVibration();
		// 振動の有効、無効の設定
		void SetVibrationEnabled(bool enabled);
		void SetVibrationEnabled(uint32_t playerIndex, bool enabled);
		void SetPlayerGamepadIndex(uint32_t playerIndex, int32_t gamepadIndex);
		int32_t GetPlayerGamepadIndex(uint32_t playerIndex) const;
		void SetPlayerKeyboardMouseEnabled(uint32_t playerIndex, bool enabled);
		bool IsPlayerKeyboardMouseEnabled(uint32_t playerIndex) const;
		void SetBackgroundInputEnabled(bool enabled) { configuration_.backgroundInputEnabled = enabled; }
		bool IsBackgroundInputEnabled() const { return configuration_.backgroundInputEnabled; }
		bool IsGameplayInputAvailable(uint32_t playerIndex) const;
		// ゲームパッドが繋がっているかどうか
		bool IsGamepadConnected() const { return hardware_.GetState().gamepadConnected; }

		//--------- ゲーム入力 ---------------------------------------------------
		// ボタンと軸の番号はC#の列挙型に対応
		bool GamepadConnectedByIndex(int index) const;
		int ConnectedGamepadCount() const;
		bool GamepadButtonByIndex(int index, int button) const;
		bool GamepadButtonDownByIndex(int index, int button) const;
		bool GamepadButtonUpByIndex(int index, int button) const;
		float GamepadAxisByIndex(int index, int axis) const;
		// このフレームの確定テキストを取得
		const std::string& FrameTextInput() const { return windowEvents_.FrameTextInput(); }
		// ウィンドウがフォーカスを持っているか
		bool HasWindowFocus() const { return windowEvents_.HasWindowFocus(); }
		// Windowの文字入力とフォーカスを通知
		void AppendTextInputUtf16(wchar_t code) { windowEvents_.AppendTextInputUtf16(code); }
		void SetWindowFocus(bool focused);

		// 外部エクスプローラーからのファイルドロップを画面座標で積む
		void PushDroppedFiles(const std::vector<std::string>& paths, const Vector2& screenPoint);
		// 溜めたファイルドロップを取り出して消費する、未着なら何もせずfalse
		bool TakeDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint);
		// 溜めたファイルドロップを消費せず取得する
		bool PeekDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) const;

		// 共有インスタンス
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

		WinApp* winApp_ = nullptr;
		InputHardware hardware_;
		InputWindowEvents windowEvents_;

		// 入力状態
		InputType inputType_ = InputType::Keyboard;

		// 保存設定と前フレームの範囲制御
		InputDeviceConfiguration configuration_;
		bool mouseRangeControlPrev_ = false;

		// 描画矩形範囲
		InputViewMapping views_;

		std::array<InputVibrationPlayer, kMaxPlayers> vibrations_{};
		bool suppressEdgesThisFrame_ = false;
		bool suppressEdgesOnNextUpdate_ = false;

		//--------- functions ----------------------------------------------------

		// 指定したマウスボタンの現在値を取得
		bool PushMouseButton(size_t index, const std::source_location& location) const;
		// キーボードかマウスの操作を検出
		bool HasKeyboardMouseInput() const;
		// ゲームパッドの操作を検出
		bool HasGamepadInput() const;

		// 生成と複製を共有インスタンスに限定
		Input() = default;
		~Input() = default;
		Input(const Input&) = delete;
		Input& operator=(const Input&) = delete;
	};
}; // Engine
