#include "InputSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================

namespace {

	// C# GamepadButton enum(0..15)をXINPUTのボタンマスクへ対応付ける、トリガは別扱いで0
	WORD XInputMaskOfGamepadButton(int button) {
		switch (button) {
		case 0:  return XINPUT_GAMEPAD_DPAD_UP;
		case 1:  return XINPUT_GAMEPAD_DPAD_DOWN;
		case 2:  return XINPUT_GAMEPAD_DPAD_LEFT;
		case 3:  return XINPUT_GAMEPAD_DPAD_RIGHT;
		case 4:  return XINPUT_GAMEPAD_START;
		case 5:  return XINPUT_GAMEPAD_BACK;
		case 6:  return XINPUT_GAMEPAD_LEFT_THUMB;
		case 7:  return XINPUT_GAMEPAD_RIGHT_THUMB;
		case 8:  return XINPUT_GAMEPAD_LEFT_SHOULDER;
		case 9:  return XINPUT_GAMEPAD_RIGHT_SHOULDER;
		case 12: return XINPUT_GAMEPAD_A;
		case 13: return XINPUT_GAMEPAD_B;
		case 14: return XINPUT_GAMEPAD_X;
		case 15: return XINPUT_GAMEPAD_Y;
		default: return 0;
		}
	}

	// 指定stateでそのボタンが押されているか、トリガ10/11は閾値判定
	bool IsGamepadButtonPressed(const XINPUT_STATE& state, int button) {
		if (button == 10) {
			return state.Gamepad.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
		}
		if (button == 11) {
			return state.Gamepad.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
		}
		const WORD mask = XInputMaskOfGamepadButton(button);
		return mask != 0 && (state.Gamepad.wButtons & mask) != 0;
	}
}

bool Input::GamepadConnectedByIndex(int index) const {
	return index >= 0 && index < InputDeviceState::kMaxGamepads && hardware_.GetState().padConnected[static_cast<size_t>(index)];
}

int Input::ConnectedGamepadCount() const {
	int count = 0;
	for (int i = 0; i < InputDeviceState::kMaxGamepads; ++i) {
		if (hardware_.GetState().padConnected[static_cast<size_t>(i)]) {
			++count;
		}
	}
	return count;
}

bool Input::GamepadButtonByIndex(int index, int button) const {
	if (!GamepadConnectedByIndex(index)) {
		return false;
	}
	return IsGamepadButtonPressed(hardware_.GetState().pads[static_cast<size_t>(index)], button);
}

bool Input::GamepadButtonDownByIndex(int index, int button) const {
	if (index < 0 || index >= InputDeviceState::kMaxGamepads) {
		return false;
	}
	const bool now = hardware_.GetState().padConnected[static_cast<size_t>(index)] && IsGamepadButtonPressed(hardware_.GetState().pads[static_cast<size_t>(index)], button);
	const bool pre = hardware_.GetState().padConnectedPre[static_cast<size_t>(index)] && IsGamepadButtonPressed(hardware_.GetState().padsPre[static_cast<size_t>(index)], button);
	return now && !pre;
}

bool Input::GamepadButtonUpByIndex(int index, int button) const {
	if (index < 0 || index >= InputDeviceState::kMaxGamepads) {
		return false;
	}
	const bool now = hardware_.GetState().padConnected[static_cast<size_t>(index)] && IsGamepadButtonPressed(hardware_.GetState().pads[static_cast<size_t>(index)], button);
	const bool pre = hardware_.GetState().padConnectedPre[static_cast<size_t>(index)] && IsGamepadButtonPressed(hardware_.GetState().padsPre[static_cast<size_t>(index)], button);
	return pre && !now;
}

float Input::GamepadAxisByIndex(int index, int axis) const {
	if (!GamepadConnectedByIndex(index)) {
		return 0.0f;
	}
	// raw値を返しdead zoneはAction Map側で適用する、stickは[-1,1]でトリガは[0,1]
	const XINPUT_GAMEPAD& pad = hardware_.GetState().pads[static_cast<size_t>(index)].Gamepad;
	switch (axis) {
	case 0:  return std::clamp(static_cast<float>(pad.sThumbLX) / 32767.0f, -1.0f, 1.0f);
	case 1:  return std::clamp(static_cast<float>(pad.sThumbLY) / 32767.0f, -1.0f, 1.0f);
	case 2:  return std::clamp(static_cast<float>(pad.sThumbRX) / 32767.0f, -1.0f, 1.0f);
	case 3:  return std::clamp(static_cast<float>(pad.sThumbRY) / 32767.0f, -1.0f, 1.0f);
	case 4:  return static_cast<float>(pad.bLeftTrigger) / 255.0f;
	case 5:  return static_cast<float>(pad.bRightTrigger) / 255.0f;
	default: return 0.0f;
	}
}
