#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
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

		void SetInputType(InputType type) { inputType_ = type; }
		InputType GetType() const { return inputType_; }

		// deadZone
		void SetDeadZone(float deadZone);
		float GetDeadZone() const { return deadZone_; }

		// maxStickValue
		float GetMaxStickValue() const { return maxStickValue_; }

		// view
		void SetViewRect(InputViewArea viewArea, const Vector2& dstPos,
			const Vector2& dstSize, const Vector2& srcSize = Vector2::AnyInit(0.0f));
		bool HasViewRect(InputViewArea viewArea) const;
		bool IsMouseOnView(InputViewArea viewArea) const;
		std::optional<Vector2> GetMousePosInView(InputViewArea viewArea) const;

		// ゲームパッドの振動
		uint32_t PlayVibration(const InputVibrationParams& params);
		// 指定のID振動の停止
		void StopVibration(uint32_t handle);
		// 全ての振動を停止
		void StopAllVibration();
		// 振動の有効、無効の設定
		void SetVibrationEnabled(bool enabled);
		// ゲームパッドが繋がっているかどうか
		bool IsGamepadConnected() const { return gamepadConnected_; }

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
		const std::string& FrameTextInput() const { return frameText_; }
		// ウィンドウがフォーカスを持っているか
		bool HasWindowFocus() const { return hasFocus_; }
		// WinAppのWM_CHAR / focusメッセージからmain threadで呼ぶ
		void AppendTextInputUtf16(wchar_t code) { pendingWide_.push_back(code); }
		void SetWindowFocus(bool focused) { hasFocus_ = focused; }

		// 外部エクスプローラーからのファイルドロップでWM_DROPFILESからmain threadで積む、点はクライアント座標
		void PushDroppedFiles(const std::vector<std::string>& paths, const Vector2& clientPoint) {
			if (paths.empty()) { return; }
			droppedFiles_ = paths;
			droppedFilesPoint_ = clientPoint;
			hasDroppedFiles_ = true;
		}
		// 溜めたファイルドロップを取り出して消費する、未着なら何もせずfalse
		bool TakeDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) {
			if (!hasDroppedFiles_) { return false; }
			outPaths = std::move(droppedFiles_);
			outClientPoint = droppedFilesPoint_;
			droppedFiles_.clear();
			hasDroppedFiles_ = false;
			return true;
		}

		// singleton
		static Input* GetInstance();
		static void Finalize();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// 描画矩形
		struct ViewRect {

			Vector2 dstPos;   // ウィンドウ内の貼り付け左上
			Vector2 dstSize;  // ウィンドウ内の貼り付けサイズ
			Vector2 srcSize;  // 元サイズ
		};

		//--------- variables ----------------------------------------------------

		static Input* instance_;

		WinApp* winApp_;

		// 入力状態
		InputType inputType_ = InputType::Keyboard;

		// key
		std::array<BYTE, 256> key_{};
		std::array<BYTE, 256> keyPre_{};

		ComPtr<IDirectInput8> dInput_;
		ComPtr<IDirectInputDevice8> keyboard_;

		// gamePad、状態はindex0の多gamepad snapshotを共有して保持する
		XINPUT_STATE gamepadState_{};
		bool gamepadConnected_ = false;

		std::array<bool, static_cast<size_t>(GamePadButtons::Counts)> gamepadButtons_{};
		std::array<bool, static_cast<size_t>(GamePadButtons::Counts)> gamepadButtonsPre_{};

		// gameplay用の独立snapshotで最大4台、既存single-gamepad pathとは別管理
		static constexpr int kMaxGamepads = 4;
		std::array<XINPUT_STATE, kMaxGamepads> pads_{};
		std::array<XINPUT_STATE, kMaxGamepads> padsPre_{};
		std::array<bool, kMaxGamepads> padConnected_{};
		std::array<bool, kMaxGamepads> padConnectedPre_{};
		// 文字入力はpendingWide_にWM_CHARを溜め、UpdateでframeText_(UTF-8)へ確定する
		std::wstring pendingWide_;
		std::string frameText_;
		// ウィンドウフォーカス状態
		bool hasFocus_ = true;

		// 外部からドロップされたファイル、クライアント座標の点と一緒に消費待ちで持つ
		std::vector<std::string> droppedFiles_{};
		Vector2 droppedFilesPoint_{};
		bool hasDroppedFiles_ = false;

		std::array<std::chrono::steady_clock::time_point, 256> keyStartTime_{};
		std::array<bool, 256> keyStayLogged_{};

		std::array<std::chrono::steady_clock::time_point, static_cast<size_t>(GamePadButtons::Counts)> gpStartTime_{};
		std::array<bool, static_cast<size_t>(GamePadButtons::Counts)> gpStayLogged_{};

		std::array<std::chrono::steady_clock::time_point, 3> mouseStartTime_{};
		std::array<bool, 3> mouseStayLogged_{};

		float leftThumbX_;
		float leftThumbY_;
		float rightThumbX_;
		float rightThumbY_;

		// デッドゾーンの閾値
		float deadZone_ = 8000.0f;
		// スティック入力の最大値
		const float maxStickValue_ = 32767.0f;

		// LTボタン
		float leftTriggerValue_ = 0.0f;
		// RTボタン
		float rightTriggerValue_ = 0.0f;

		// mouse
		DIMOUSESTATE mouseState_;

		ComPtr<IDirectInputDevice8> mouse_; // マウスデバイス

		std::array<bool, 3> mouseButtons_;    // マウスボタンの状態
		std::array<bool, 3> mousePreButtons_; // 1フレ前のマウスボタンの状態
		Vector2 mousePos_;                    // マウスの座標
		Vector2 mousePrePos_;                 // マウスの前座標
		float wheelValue_;                    // ホイール移動量

		// 描画矩形範囲
		std::unordered_map<InputViewArea, ViewRect> viewRects_;

		// 振動エフェクト
		struct VibrationEffect {

			uint32_t handle = 0;
			float left = 0.0f;     // 0..1
			float right = 0.0f;    // 0..1
			float duration = 0.0f; // seconds
			float attack = 0.0f;   // seconds
			float release = 0.0f;  // seconds
			int priority = 0;
			std::chrono::steady_clock::time_point start;
		};
		bool vibrationEnabled_ = true;
		std::vector<VibrationEffect> vibEffects_{};
		uint32_t nextVibHandle_ = 1;
		uint16_t lastMotorLeft_ = 0;
		uint16_t lastMotorRight_ = 0;

		//--------- functions ----------------------------------------------------

		// helper
		bool PushMouseButton(size_t index, const std::source_location& location) const;
		float ApplyDeadZone(float value);

		// ゲームパッド
		void UpdateVibration();
		void ApplyVibration(uint16_t left, uint16_t right);
		static uint16_t ToMotorSpeed(float v01);

		Input() = default;
		~Input() = default;
		Input(const Input&) = delete;
		Input& operator=(const Input&) = delete;
	};
}; // Engine
