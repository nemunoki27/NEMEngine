#pragma once

//============================================================================
//	include
//============================================================================
#include "InputTypes.h"
#include <Engine/Core/Foundation/Math/Vector2.h>

// c++
#include <array>
// windows
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <XInput.h>

namespace Engine {

	// 入力機器の現在値と前frame値
	struct InputDeviceState {

		static constexpr int kMaxGamepads = 4;
		std::array<BYTE, 256> key{};
		std::array<BYTE, 256> keyPre{};
		XINPUT_STATE gamepadState{};
		bool gamepadConnected = false;
		std::array<bool, static_cast<size_t>(GamePadButtons::Counts)> gamepadButtons{};
		std::array<bool, static_cast<size_t>(GamePadButtons::Counts)> gamepadButtonsPre{};
		std::array<XINPUT_STATE, kMaxGamepads> pads{};
		std::array<XINPUT_STATE, kMaxGamepads> padsPre{};
		std::array<bool, kMaxGamepads> padConnected{};
		std::array<bool, kMaxGamepads> padConnectedPre{};
		float leftThumbX;
		float leftThumbY;
		float rightThumbX;
		float rightThumbY;
		float leftTriggerValue = 0.0f;
		float rightTriggerValue = 0.0f;
		DIMOUSESTATE mouseState;
		std::array<bool, 3> mouseButtons;    // マウスボタンの状態
		std::array<bool, 3> mousePreButtons; // 1フレ前のマウスボタンの状態
		Vector2 mousePos;                    // マウスの座標
		Vector2 mouseScreenPos;              // デスクトップ上のマウス座標
		Vector2 mousePrePos;                 // マウスの前座標
		float wheelValue;                    // ホイール移動量
	};
}
