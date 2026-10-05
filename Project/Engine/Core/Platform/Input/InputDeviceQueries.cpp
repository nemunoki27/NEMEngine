#include "InputSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <algorithm>

using namespace Engine;

bool Input::PushMouseLeft(const std::source_location& location) const {

	return PushMouseButton(0, location);
}

bool Input::PushMouseRight(const std::source_location& location) const {

	return PushMouseButton(1, location);
}

bool Input::PushMouseCenter(const std::source_location& location) const {

	return PushMouseButton(2, location);
}

namespace {

	// GamepadButtonをボタンマスクへ変換する
	WORD XInputMaskOfGamepadButton(int button) {
		switch (button) {
		case 0:
			return XINPUT_GAMEPAD_DPAD_UP;
		case 1:
			return XINPUT_GAMEPAD_DPAD_DOWN;
		case 2:
			return XINPUT_GAMEPAD_DPAD_LEFT;
		case 3:
			return XINPUT_GAMEPAD_DPAD_RIGHT;
		case 4:
			return XINPUT_GAMEPAD_START;
		case 5:
			return XINPUT_GAMEPAD_BACK;
		case 6:
			return XINPUT_GAMEPAD_LEFT_THUMB;
		case 7:
			return XINPUT_GAMEPAD_RIGHT_THUMB;
		case 8:
			return XINPUT_GAMEPAD_LEFT_SHOULDER;
		case 9:
			return XINPUT_GAMEPAD_RIGHT_SHOULDER;
		case 12:
			return XINPUT_GAMEPAD_A;
		case 13:
			return XINPUT_GAMEPAD_B;
		case 14:
			return XINPUT_GAMEPAD_X;
		case 15:
			return XINPUT_GAMEPAD_Y;
		default:
			return 0;
		}
	}

	// ボタンとトリガの押下状態を判定する
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
	return index >= 0 && index < InputDeviceState::kMaxGamepads &&
		   hardware_.GetState().padConnected[static_cast<size_t>(index)];
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
	if (suppressEdgesThisFrame_) {
		return false;
	}
	if (index < 0 || index >= InputDeviceState::kMaxGamepads) {
		return false;
	}
	if (!hardware_.GetState().padConnectedPre[static_cast<size_t>(index)]) {
		return false;
	}
	const bool now = hardware_.GetState().padConnected[static_cast<size_t>(index)] &&
					 IsGamepadButtonPressed(hardware_.GetState().pads[static_cast<size_t>(index)], button);
	const bool pre = hardware_.GetState().padConnectedPre[static_cast<size_t>(index)] &&
					 IsGamepadButtonPressed(hardware_.GetState().padsPre[static_cast<size_t>(index)], button);
	return now && !pre;
}

bool Input::GamepadButtonUpByIndex(int index, int button) const {
	if (suppressEdgesThisFrame_) {
		return false;
	}
	if (index < 0 || index >= InputDeviceState::kMaxGamepads) {
		return false;
	}
	if (!hardware_.GetState().padConnected[static_cast<size_t>(index)] ||
		!hardware_.GetState().padConnectedPre[static_cast<size_t>(index)]) {
		return false;
	}
	const bool now = hardware_.GetState().padConnected[static_cast<size_t>(index)] &&
					 IsGamepadButtonPressed(hardware_.GetState().pads[static_cast<size_t>(index)], button);
	const bool pre = hardware_.GetState().padConnectedPre[static_cast<size_t>(index)] &&
					 IsGamepadButtonPressed(hardware_.GetState().padsPre[static_cast<size_t>(index)], button);
	return pre && !now;
}

float Input::GamepadAxisByIndex(int index, int axis) const {
	if (!GamepadConnectedByIndex(index)) {
		return 0.0f;
	}
	// 軸を正規化し、deadzoneはAction側で適用する
	const XINPUT_GAMEPAD& pad = hardware_.GetState().pads[static_cast<size_t>(index)].Gamepad;
	switch (axis) {
	case 0:
		return std::clamp(static_cast<float>(pad.sThumbLX) / 32767.0f, -1.0f, 1.0f);
	case 1:
		return std::clamp(static_cast<float>(pad.sThumbLY) / 32767.0f, -1.0f, 1.0f);
	case 2:
		return std::clamp(static_cast<float>(pad.sThumbRX) / 32767.0f, -1.0f, 1.0f);
	case 3:
		return std::clamp(static_cast<float>(pad.sThumbRY) / 32767.0f, -1.0f, 1.0f);
	case 4:
		return static_cast<float>(pad.bLeftTrigger) / 255.0f;
	case 5:
		return static_cast<float>(pad.bRightTrigger) / 255.0f;
	default:
		return 0.0f;
	}
}

//============================================================================
//	Input classMethods
//============================================================================

bool Input::PushKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

	return hardware_.GetState().key[keyNumber];
}

bool Input::TriggerKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

	if (suppressEdgesThisFrame_) {
		return false;
	}
	// 前frameから押下へ変わったキーを返す
	return hardware_.GetState().key[keyNumber] && !hardware_.GetState().keyPre[keyNumber];
}
bool Input::ReleaseKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

	if (suppressEdgesThisFrame_) {
		return false;
	}
	return !hardware_.GetState().key[keyNumber] && hardware_.GetState().keyPre[keyNumber];
}
bool Input::PushGamepadButton(GamePadButtons button, [[maybe_unused]] const std::source_location& location) {

	const size_t index = static_cast<size_t>(button);
	if (hardware_.GetState().gamepadButtons.size() <= index) {
		Assert::Call(false, "GamePad Button番号が範囲外です");
		return false;
	}
	return hardware_.GetState().gamepadButtons[index];
}
bool Input::TriggerGamepadButton(GamePadButtons button, [[maybe_unused]] const std::source_location& location) {

	if (suppressEdgesThisFrame_) {
		return false;
	}
	if (!hardware_.GetState().padConnectedPre[0]) {
		return false;
	}
	// ボタン番号が範囲外の場合はfalseを返す
	if (hardware_.GetState().gamepadButtons.size() <= static_cast<size_t>(button)) {
		return false;
	}

	return hardware_.GetState().gamepadButtons[static_cast<size_t>(button)] &&
		   !hardware_.GetState().gamepadButtonsPre[static_cast<size_t>(button)];
}
float Input::GetLeftTriggerValue() const {

	return hardware_.GetState().leftTriggerValue;
}
float Input::GetRightTriggerValue() const {

	return hardware_.GetState().rightTriggerValue;
}
Vector2 Input::GetLeftStickVal() const {
	return {hardware_.GetState().leftThumbX, hardware_.GetState().leftThumbY};
}
Vector2 Input::GetRightStickVal() const {
	return {hardware_.GetState().rightThumbX, hardware_.GetState().rightThumbY};
}

Vector2 Input::GetMousePos() const {

	return hardware_.GetState().mousePos;
}
Vector2 Input::GetMousePrePos() const {

	return hardware_.GetState().mousePrePos;
}
Vector2 Input::GetMouseMoveValue() const {

	return {static_cast<float>(hardware_.GetState().mouseState.lX), static_cast<float>(hardware_.GetState().mouseState.lY)};
}
float Input::GetMouseWheel() {

	return hardware_.GetState().wheelValue;
}
bool Input::PushMouseButton(size_t index, [[maybe_unused]] const std::source_location& location) const {

	Assert::Call(index < hardware_.GetState().mouseButtons.size(), "Mouse Button番号が範囲外です");
	return index < hardware_.GetState().mouseButtons.size() && hardware_.GetState().mouseButtons[index];
}
bool Input::PushMouse(MouseButton button, const std::source_location& location) const {

	bool push = false;
	switch (button) {
	case MouseButton::Right: {

		push = PushMouseRight(location);
		break;
	}
	case MouseButton::Left: {

		push = PushMouseLeft(location);
		break;
	}
	case MouseButton::Center: {

		push = PushMouseCenter(location);
		break;
	}
	}
	return push;
}
bool Input::TriggerMouseLeft([[maybe_unused]] const std::source_location& location) const {

	if (suppressEdgesThisFrame_) {
		return false;
	}
	return !hardware_.GetState().mousePreButtons[0] && hardware_.GetState().mouseButtons[0];
}
bool Input::TriggerMouseRight([[maybe_unused]] const std::source_location& location) const {

	if (suppressEdgesThisFrame_) {
		return false;
	}
	return !hardware_.GetState().mousePreButtons[1] && hardware_.GetState().mouseButtons[1];
}
bool Input::TriggerMouseCenter([[maybe_unused]] const std::source_location& location) const {

	if (suppressEdgesThisFrame_) {
		return false;
	}
	return !hardware_.GetState().mousePreButtons[2] && hardware_.GetState().mouseButtons[2];
}
bool Input::TriggerMouse(MouseButton button, const std::source_location& location) const {

	bool trigger = false;
	switch (button) {
	case MouseButton::Right: {

		trigger = TriggerMouseRight(location);
		break;
	}
	case MouseButton::Left: {

		trigger = TriggerMouseLeft(location);
		break;
	}
	case MouseButton::Center: {

		trigger = TriggerMouseCenter(location);
		break;
	}
	}
	return trigger;
}
bool Input::ReleaseMouse(MouseButton button, [[maybe_unused]] const std::source_location& location) const {

	if (suppressEdgesThisFrame_) {
		return false;
	}
	bool released = false;
	switch (button) {
	case MouseButton::Left: {

		released = !hardware_.GetState().mouseButtons[0] && hardware_.GetState().mousePreButtons[0];
		break;
	}
	case MouseButton::Right: {

		released = !hardware_.GetState().mouseButtons[1] && hardware_.GetState().mousePreButtons[1];
		break;
	}
	case MouseButton::Center: {

		released = !hardware_.GetState().mouseButtons[2] && hardware_.GetState().mousePreButtons[2];
		break;
	}
	}
	return released;
}
