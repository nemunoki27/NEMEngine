#include "InputSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

// c++
#include <algorithm>
#include <cmath>

#pragma comment(lib, "dInput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

using namespace Engine;

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

void Input::SetDeadZone(float deadZone) {

	// 非有限値では現在の閾値を維持
	if (std::isfinite(deadZone)) {
		configuration_.deadZone = std::clamp(deadZone, 0.0f, InputDeviceConfiguration::kMaxStickValue);
	}
}

void Input::SetMouseArea(const Vector2& pos, const Vector2& size) {

	// 次の更新で使用する移動範囲を設定
	configuration_.mouseAreaPos = pos;
	configuration_.mouseAreaSize = size;
}

void Input::SetMouseReleaseShortcut(int32_t modKey, int32_t triggerKey) {

	// 解除に使用するキーを設定
	configuration_.mouseReleaseModKey = modKey;
	configuration_.mouseReleaseTriggerKey = triggerKey;
}

namespace {
	// 入力デバイス設定の保存先
	constexpr const char* kInputDeviceConfigPath = ConfigPaths::kInputDevice;
}

void Input::LoadConfig() {

	const auto path = RuntimePaths::GetUserSettingsPath(kInputDeviceConfigPath);
	if (!JsonAdapter::Check(path)) {
		return;
	}
	nlohmann::json data;
	// 検証に失敗した設定は適用しない
	if (!JsonAdapter::TryLoad(path, data) || !InputDeviceConfigurationSerialization::TryRead(data, configuration_)) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "入力デバイス設定の読み込みに失敗しました");
	}
}

void Input::SaveConfig() const {

	// 保存失敗を無視せず通知する
	if (!JsonAdapter::Save(RuntimePaths::GetUserSettingsPath(kInputDeviceConfigPath),
			InputDeviceConfigurationSerialization::Write(configuration_))) {
		Logger::Output(LogType::Engine, spdlog::level::warn, "入力デバイス設定の保存に失敗しました");
	}
}

void Input::UpdateInputDevice() {

	// 同一フレームではキーボードとマウスの操作を優先する
	if (HasKeyboardMouseInput()) {
		inputType_ = InputType::Keyboard;
	} else if (HasGamepadInput()) {
		inputType_ = InputType::GamePad;
	}

	// 指定キーの組合せで範囲制御を解除
	if (configuration_.mouseRangeControl && PushKey(static_cast<BYTE>(configuration_.mouseReleaseModKey)) &&
		TriggerKey(static_cast<BYTE>(configuration_.mouseReleaseTriggerKey))) {
		configuration_.mouseRangeControl = false;
	}

	// 現在の移動範囲をカーソルへ適用
	if (configuration_.mouseRangeControl) {
		WinApp::ClipCursorToClientRect(configuration_.mouseAreaSize, configuration_.mouseAreaPos);
	} else if (mouseRangeControlPrev_) {
		WinApp::ReleaseCursorClip();
	}
	mouseRangeControlPrev_ = configuration_.mouseRangeControl;
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
		const XINPUT_GAMEPAD previous =
			hardware_.GetState().padConnectedPre[index] ? hardware_.GetState().padsPre[index].Gamepad : XINPUT_GAMEPAD{};
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
			return currentLength > configuration_.deadZone && previousLength <= configuration_.deadZone;
		};
		if (stickStarted(current.sThumbLX, current.sThumbLY, previous.sThumbLX, previous.sThumbLY) ||
			stickStarted(current.sThumbRX, current.sThumbRY, previous.sThumbRX, previous.sThumbRY)) {
			return true;
		}
	}
	return false;
}

//============================================================================
//	ゲーム入力
//============================================================================

//============================================================================
//	InputSystem classMethods
//============================================================================

uint32_t Engine::Input::PlayVibration(const InputVibrationParams& params) {

	return PlayVibration(0, params);
}

uint32_t Engine::Input::PlayVibration(uint32_t playerIndex, const InputVibrationParams& params) {

	return playerIndex < kMaxPlayers ? vibrations_[playerIndex].PlayVibration(params) : 0;
}

void Engine::Input::StopVibration(uint32_t handle) {

	StopVibration(0, handle);
}

void Engine::Input::StopVibration(uint32_t playerIndex, uint32_t handle) {

	if (playerIndex < kMaxPlayers) {
		vibrations_[playerIndex].StopVibration(handle);
	}
}

void Engine::Input::StopAllVibration() {

	for (InputVibrationPlayer& vibration : vibrations_) {
		vibration.StopAllVibration();
	}
}

void Engine::Input::SetVibrationEnabled(bool enabled) {

	for (InputVibrationPlayer& vibration : vibrations_) {
		vibration.SetVibrationEnabled(enabled);
	}
}

void Engine::Input::SetVibrationEnabled(uint32_t playerIndex, bool enabled) {

	if (playerIndex < kMaxPlayers) {
		vibrations_[playerIndex].SetVibrationEnabled(enabled);
	}
}

void Engine::Input::SetPlayerGamepadIndex(uint32_t playerIndex, int32_t gamepadIndex) {

	if (playerIndex >= kMaxPlayers || gamepadIndex < -1 || InputDeviceState::kMaxGamepads <= gamepadIndex) {
		return;
	}
	configuration_.playerGamepads[playerIndex] = gamepadIndex;
}

int32_t Engine::Input::GetPlayerGamepadIndex(uint32_t playerIndex) const {

	return playerIndex < kMaxPlayers ? configuration_.playerGamepads[playerIndex] : -1;
}

void Engine::Input::SetPlayerKeyboardMouseEnabled(uint32_t playerIndex, bool enabled) {

	if (playerIndex < kMaxPlayers) {
		configuration_.playerKeyboardMouse[playerIndex] = enabled;
	}
}

bool Engine::Input::IsPlayerKeyboardMouseEnabled(uint32_t playerIndex) const {

	return playerIndex < kMaxPlayers && configuration_.playerKeyboardMouse[playerIndex];
}

bool Engine::Input::IsGameplayInputAvailable(uint32_t playerIndex) const {

	return playerIndex < kMaxPlayers && (configuration_.backgroundInputEnabled || HasWindowFocus());
}

void Input::Init(WinApp* winApp) {

	winApp_ = winApp;
	hardware_.Init(*winApp_);
	for (uint32_t i = 0; i < kMaxPlayers; ++i) {
		vibrations_[i].SetGamepadIndex(i);
	}
	LoadConfig();
}

void Input::Update() {

	// フォーカス復帰時は押下状態だけ復元
	suppressEdgesThisFrame_ = suppressEdgesOnNextUpdate_;
	suppressEdgesOnNextUpdate_ = false;
	hardware_.BeginFrame();
	hardware_.PollKeyboardAndGamepads(configuration_.deadZone);
	windowEvents_.CommitText();

	// Playerごとの割り当て先へ振動を出力
	for (uint32_t i = 0; i < kMaxPlayers; ++i) {
		const int32_t gamepadIndex = configuration_.playerGamepads[i];
		vibrations_[i].SetGamepadIndex(gamepadIndex < 0 ? i : static_cast<uint32_t>(gamepadIndex));
		vibrations_[i].UpdateVibration(IsGameplayInputAvailable(i) && GamepadConnectedByIndex(gamepadIndex));
	}

	hardware_.PollMouse(*winApp_);
}

void Input::SetWindowFocus(bool focused) {

	if (focused && !windowEvents_.HasWindowFocus()) {
		suppressEdgesOnNextUpdate_ = true;
	}
	windowEvents_.SetWindowFocus(focused);
}

void Input::SetViewRect(InputViewArea viewArea, const Vector2& dstPos, const Vector2& dstSize, const Vector2& srcSize,
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
