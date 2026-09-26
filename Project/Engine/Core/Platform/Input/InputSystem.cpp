#include "InputSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

#pragma comment(lib,"dInput8.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib, "xinput.lib")

//============================================================================
//	Input classMethods
//============================================================================

Input* Input::instance_ = nullptr;

Input* Input::GetInstance() {

	if (instance_ == nullptr) {
		instance_ = new Input();
	}
	return instance_;
}

Input* Input::TryGetInstance() {

	return instance_;
}

void Input::Finalize() {

	if (instance_ != nullptr) {

		instance_->StopAllVibration();

		delete instance_;
		instance_ = nullptr;
	}
}

bool Input::PushKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

	return hardware_.GetState().key[keyNumber];
}

bool Input::TriggerKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

	// 現在のフレームで押されていて、前のフレームで押されていなかった場合にtrueを返す
	return hardware_.GetState().key[keyNumber] && !hardware_.GetState().keyPre[keyNumber];
}
bool Input::ReleaseKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

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
	return { hardware_.GetState().leftThumbX,hardware_.GetState().leftThumbY };
}
Vector2 Input::GetRightStickVal() const {
	return { hardware_.GetState().rightThumbX,hardware_.GetState().rightThumbY };
}

Vector2 Input::GetMousePos() const {

	return hardware_.GetState().mousePos;
}
Vector2 Input::GetMousePrePos() const {

	return hardware_.GetState().mousePrePos;
}
Vector2 Input::GetMouseMoveValue() const {

	return { static_cast<float>(hardware_.GetState().mouseState.lX),static_cast<float>(hardware_.GetState().mouseState.lY) };
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

	return !hardware_.GetState().mousePreButtons[0] && hardware_.GetState().mouseButtons[0];
}
bool Input::TriggerMouseRight([[maybe_unused]] const std::source_location& location) const {

	return !hardware_.GetState().mousePreButtons[1] && hardware_.GetState().mouseButtons[1];
}
bool Input::TriggerMouseCenter([[maybe_unused]] const std::source_location& location) const {

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
void Input::SetDeadZone(float deadZone) {

	deadZone_ = std::clamp(deadZone, 0.0f, maxStickValue_);
}

namespace {
	// 入力デバイス設定の保存先
	constexpr const char* kInputDeviceConfigPath = ConfigPaths::kInputDevice;
}

void Input::LoadConfig() {

	const std::filesystem::path path = RuntimePaths::GetUserSettingsPath(kInputDeviceConfigPath);
	if (!JsonAdapter::Check(path)) {
		return;
	}
	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

	deadZone_ = data.value("deadZone", deadZone_);
	SetDeadZone(deadZone_);

	// マウス範囲制御を復元する
	mouseRangeControl_ = data.value("mouseRangeControl", mouseRangeControl_);
	mouseAreaPos_.x = data.value("mouseAreaPosX", mouseAreaPos_.x);
	mouseAreaPos_.y = data.value("mouseAreaPosY", mouseAreaPos_.y);
	mouseAreaSize_.x = data.value("mouseAreaSizeX", mouseAreaSize_.x);
	mouseAreaSize_.y = data.value("mouseAreaSizeY", mouseAreaSize_.y);
	mouseReleaseModKey_ = data.value("mouseReleaseModKey", mouseReleaseModKey_);
	mouseReleaseTriggerKey_ = data.value("mouseReleaseTriggerKey", mouseReleaseTriggerKey_);
}

void Input::SaveConfig() const {

	nlohmann::json data{};
	data["deadZone"] = deadZone_;

	data["mouseRangeControl"] = mouseRangeControl_;
	data["mouseAreaPosX"] = mouseAreaPos_.x;
	data["mouseAreaPosY"] = mouseAreaPos_.y;
	data["mouseAreaSizeX"] = mouseAreaSize_.x;
	data["mouseAreaSizeY"] = mouseAreaSize_.y;
	data["mouseReleaseModKey"] = mouseReleaseModKey_;
	data["mouseReleaseTriggerKey"] = mouseReleaseTriggerKey_;

	JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(kInputDeviceConfigPath), data);
}

void Input::UpdateInputDevice() {

	// 同一フレームではキーボードとマウスの操作を優先する
	if (HasKeyboardMouseInput()) {
		inputType_ = InputType::Keyboard;
	} else if (HasGamepadInput()) {
		inputType_ = InputType::GamePad;
	}

	// 範囲制御中はショートカット(modKey押下+triggerKey)で解除できるようにする
	if (mouseRangeControl_ &&
		PushKey(static_cast<BYTE>(mouseReleaseModKey_)) &&
		TriggerKey(static_cast<BYTE>(mouseReleaseTriggerKey_))) {
		mouseRangeControl_ = false;
	}

	// 範囲制御中は毎フレーム指定矩形へクリップし直し、位置とサイズの変更も反映する
	if (mouseRangeControl_) {
		WinApp::ClipCursorToClientRect(mouseAreaSize_, mouseAreaPos_);
	} else if (mouseRangeControlPrev_) {
		WinApp::ReleaseCursorClip();
	}
	mouseRangeControlPrev_ = mouseRangeControl_;
}

bool Input::HasKeyboardMouseInput() const {

	// 任意キーが押されたフレームをPC操作として扱う
	for (size_t i = 0; i < hardware_.GetState().key.size(); ++i) {
		if (hardware_.GetState().key[i] && !hardware_.GetState().keyPre[i]) {
			return true;
		}
	}

	// マウスボタンが押されたフレームをPC操作として扱う
	for (size_t i = 0; i < hardware_.GetState().mouseButtons.size(); ++i) {
		if (hardware_.GetState().mouseButtons[i] && !hardware_.GetState().mousePreButtons[i]) {
			return true;
		}
	}

	// 微小な揺れを除いたマウス移動とホイール操作を検出する
	constexpr float kMouseMoveThreshold = 2.0f;
	const Vector2 move = GetMouseMoveValue();
	if (std::sqrt(move.x * move.x + move.y * move.y) > kMouseMoveThreshold) {
		return true;
	}
	return hardware_.GetState().wheelValue != 0.0f;
}

bool Input::HasGamepadInput() const {

	// 接続状態では切り替えず、ボタンとアナログ入力の開始だけを操作として扱う
	for (int i = 0; i < InputDeviceState::kMaxGamepads; ++i) {
		const size_t index = static_cast<size_t>(i);
		if (!hardware_.GetState().padConnected[index]) {
			continue;
		}

		const XINPUT_GAMEPAD& current = hardware_.GetState().pads[index].Gamepad;
		const XINPUT_GAMEPAD previous = hardware_.GetState().padConnectedPre[index]
			? hardware_.GetState().padsPre[index].Gamepad : XINPUT_GAMEPAD{};
		if ((current.wButtons & ~previous.wButtons) != 0) {
			return true;
		}
		if ((current.bLeftTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD &&
			previous.bLeftTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) ||
			(current.bRightTrigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD &&
				previous.bRightTrigger <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD)) {
			return true;
		}

		const auto stickStarted = [this](SHORT x, SHORT y, SHORT previousX, SHORT previousY) {
			const float currentX = static_cast<float>(x);
			const float currentY = static_cast<float>(y);
			const float preX = static_cast<float>(previousX);
			const float preY = static_cast<float>(previousY);
			const float currentLength = std::sqrt(currentX * currentX + currentY * currentY);
			const float previousLength = std::sqrt(preX * preX + preY * preY);
			return currentLength > deadZone_ && previousLength <= deadZone_;
		};
		if (stickStarted(current.sThumbLX, current.sThumbLY,
			previous.sThumbLX, previous.sThumbLY) ||
			stickStarted(current.sThumbRX, current.sThumbRY,
				previous.sThumbRX, previous.sThumbRY)) {
			return true;
		}
	}
	return false;
}

//============================================================================
//	gameplay向け多gamepad / text / focus
//============================================================================

//============================================================================
//	InputSystem classMethods
//============================================================================

uint32_t Engine::Input::PlayVibration(const InputVibrationParams& params) {

	return vibration_.PlayVibration(params);
}

void Engine::Input::StopVibration(uint32_t handle) {

	vibration_.StopVibration(handle);
}

void Engine::Input::StopAllVibration() {

	vibration_.StopAllVibration();
}

void Engine::Input::SetVibrationEnabled(bool enabled) {

	vibration_.SetVibrationEnabled(enabled);
}

void Input::Init(WinApp* winApp) {

	winApp_ = winApp;
	hardware_.Init(*winApp_);
	LoadConfig();
}

void Input::Update() {

	hardware_.BeginFrame();
	hardware_.PollKeyboardAndGamepads(deadZone_);
	windowEvents_.CommitText();

	// デバイス振動の更新
	vibration_.UpdateVibration(hardware_.GetState().gamepadConnected);

	hardware_.PollMouse(*winApp_);
}

void Input::SetViewRect(InputViewArea viewArea, const Vector2& dstPos,
	const Vector2& dstSize, const Vector2& srcSize,
	InputViewCoordinateSpace coordinateSpace) {

	views_.SetViewRect(viewArea, dstPos, dstSize, srcSize, coordinateSpace);
}

bool Engine::Input::HasViewRect(InputViewArea viewArea) const {

	return views_.HasViewRect(viewArea);
}

bool Input::IsMouseOnView(InputViewArea viewArea) const {

	return views_.IsMouseOnView(viewArea, hardware_.GetState());
}

std::optional<Vector2> Input::GetMousePosInView(InputViewArea viewArea) const {

	return views_.GetMousePosInView(viewArea, hardware_.GetState());
}

Vector2 Input::GetMouseMoveValueInView(InputViewArea viewArea) const {

	return views_.GetMouseMoveValueInView(viewArea, GetMouseMoveValue());
}

void Input::PushDroppedFiles(const std::vector<std::string>& paths, const Vector2& screenPoint) {

	windowEvents_.PushDroppedFiles(paths, screenPoint);
}

bool Input::TakeDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) {

	return windowEvents_.TakeDroppedFiles(outPaths, outClientPoint);
}

bool Input::PeekDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) const {

	return windowEvents_.PeekDroppedFiles(outPaths, outClientPoint);
}
