#include "InputHardware.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <algorithm>
#include <cstring>
#include <cmath>

#pragma comment(lib,"dInput8.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"xinput.lib")

namespace {

	float ApplyDeadZone(float value, float deadZone) {

		if (std::fabs(value) < deadZone) {
			return 0.0f;
		}
		return value;
	}
}

void InputHardware::Init(WinApp& winApp) {

	HRESULT hr;

	// DirectInputの初期化
	dInput_ = nullptr;
	hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&dInput_, nullptr);
	Assert::Call(SUCCEEDED(hr), "DirectInputの初期化に失敗しました");

	// キーボードデバイスの初期化
	keyboard_ = nullptr;
	hr = dInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
	Assert::Call(SUCCEEDED(hr), "キーボード入力デバイスの作成に失敗しました");

	// 入力データ形式のセット標準形式
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	Assert::Call(SUCCEEDED(hr), "キーボード入力形式の設定に失敗しました");

	// 排他制御レベルのリセット
	hr = keyboard_->SetCooperativeLevel(winApp.GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	Assert::Call(SUCCEEDED(hr), "キーボードの協調レベル設定に失敗しました");

	// マウスデバイスの初期化
	hr = dInput_->CreateDevice(GUID_SysMouse, &mouse_, NULL);
	Assert::Call(SUCCEEDED(hr), "マウス入力デバイスの作成に失敗しました");

	// 入力データ形式のセット
	hr = mouse_->SetDataFormat(&c_dfDIMouse);
	Assert::Call(SUCCEEDED(hr), "マウス入力形式の設定に失敗しました");

	// 排他制御レベルのリセット
	hr = mouse_->SetCooperativeLevel(winApp.GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	Assert::Call(SUCCEEDED(hr), "マウスの協調レベル設定に失敗しました");

	// マウスの取得開始
	hr = mouse_->Acquire();

}

void InputHardware::BeginFrame() {

	auto& state = state_;
	state.mousePrePos = state.mousePos;
	state.mousePreButtons = state.mouseButtons;
}

void InputHardware::PollKeyboardAndGamepads(float deadZone) {

	auto& state = state_;
	HRESULT hr;

	// キーボード情報の取得開始
	hr = keyboard_->Acquire();

	// 前回のキー入力を保存
	std::memcpy(state.keyPre.data(), state.key.data(), state.key.size());

	// 全キーの入力状態を取得する
	hr = keyboard_->GetDeviceState(static_cast<DWORD>(state.key.size()), state.key.data());

	// 前回のゲームパッドの状態を保存
	std::memcpy(state.gamepadButtonsPre.data(), state.gamepadButtons.data(), state.gamepadButtons.size());

	// gameplay用の多gamepad snapshotを更新する、indexはC# GamepadButton / GamepadAxis enumに対応する
	state.padsPre = state.pads;
	state.padConnectedPre = state.padConnected;
	for (int i = 0; i < InputDeviceState::kMaxGamepads; ++i) {
		ZeroMemory(&state.pads[i], sizeof(XINPUT_STATE));
		state.padConnected[i] = (XInputGetState(static_cast<DWORD>(i), &state.pads[i]) == ERROR_SUCCESS);
	}

	// 既存single-gamepad pathはindex0のsnapshotを共有し、XInputGetStateの二重ポーリングを避ける
	state.gamepadState = state.pads[0];
	state.gamepadConnected = state.padConnected[0];

	if (state.gamepadConnected) {

#pragma region ///ゲームパッドが接続されている場合の処理 ///
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::ARROW_UP)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::ARROW_DOWN)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::ARROW_LEFT)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::ARROW_RIGHT)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::START)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::BACK)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::LEFT_THUMB)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::RIGHT_THUMB)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::LEFT_SHOULDER)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::RIGHT_SHOULDER)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::LEFT_TRIGGER)] = (state.leftTriggerValue > 0);
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::RIGHT_TRIGGER)] = (state.rightTriggerValue > 0);
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::A)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::B)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::X)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
		state.gamepadButtons[static_cast<size_t>(GamePadButtons::Y)] = (state.gamepadState.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;

		// スティックの状態を更新
		state.leftThumbX = ApplyDeadZone(state.gamepadState.Gamepad.sThumbLX, deadZone);
		state.leftThumbY = ApplyDeadZone(state.gamepadState.Gamepad.sThumbLY, deadZone);
		state.rightThumbX = ApplyDeadZone(state.gamepadState.Gamepad.sThumbRX, deadZone);
		state.rightThumbY = ApplyDeadZone(state.gamepadState.Gamepad.sThumbRY, deadZone);

		state.leftTriggerValue = static_cast<float>(state.gamepadState.Gamepad.bLeftTrigger) / 255.0f;
		state.rightTriggerValue = static_cast<float>(state.gamepadState.Gamepad.bRightTrigger) / 255.0f;
#pragma endregion
	} else {

		// ゲームパッドが接続されていない場合の処理
		std::fill(state.gamepadButtons.begin(), state.gamepadButtons.end(), false);

		state.leftThumbX = 0.0f;
		state.leftThumbY = 0.0f;
		state.rightThumbX = 0.0f;
		state.rightThumbY = 0.0f;

		state.leftTriggerValue = 0.0f;
		state.rightTriggerValue = 0.0f;
	}

}

void InputHardware::PollMouse(WinApp& winApp) {

	auto& state = state_;
	HRESULT hr;
	// マウス情報の取得開始
	hr = mouse_->Acquire();
	if (FAILED(hr)) {
		if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {

			mouse_->Acquire();
		}
	}

	hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &state.mouseState);

	POINT screenPoint{};
	if (GetCursorPos(&screenPoint)) {
		state.mouseScreenPos.x = static_cast<float>(screenPoint.x);
		state.mouseScreenPos.y = static_cast<float>(screenPoint.y);

		POINT clientPoint = screenPoint;
		ScreenToClient(winApp.GetHwnd(), &clientPoint);
		state.mousePos.x = static_cast<float>(clientPoint.x);
		state.mousePos.y = static_cast<float>(clientPoint.y);
	}

	if (FAILED(hr)) {
		// 取得失敗時の処理
		ZeroMemory(&state.mouseState, sizeof(DIMOUSESTATE));
		std::fill(state.mouseButtons.begin(), state.mouseButtons.end(), false);
		state.wheelValue = 0.0f;
	} else {

		// マウスボタンの状態を保存
		state.mouseButtons[0] = (state.mouseState.rgbButtons[0] & 0x80) != 0;
		state.mouseButtons[1] = (state.mouseState.rgbButtons[1] & 0x80) != 0;
		state.mouseButtons[2] = (state.mouseState.rgbButtons[2] & 0x80) != 0;

		// ホイール値
		state.wheelValue = static_cast<float>(state.mouseState.lZ) / WHEEL_DELTA;
	}

}
