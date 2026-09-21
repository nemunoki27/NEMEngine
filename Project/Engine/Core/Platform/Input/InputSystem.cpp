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

void Input::SetViewRect(InputViewArea viewArea, const Vector2& dstPos,
	const Vector2& dstSize, const Vector2& srcSize,
	InputViewCoordinateSpace coordinateSpace) {

	ViewRect& rect = viewRects_[viewArea];
	rect.dstPos = dstPos;
	rect.dstSize = dstSize;
	rect.coordinateSpace = coordinateSpace;
	if (srcSize.x <= 0.0f || srcSize.y <= 0.0f) {
		rect.srcSize = dstSize;
	} else {
		rect.srcSize = srcSize;
	}
}

bool Engine::Input::HasViewRect(InputViewArea viewArea) const {

	return viewRects_.find(viewArea) != viewRects_.end();
}

bool Input::IsMouseOnView(InputViewArea viewArea) const {

	auto found = viewRects_.find(viewArea);
	if (found == viewRects_.end()) {
		return false;
	}
	const ViewRect& rect = found->second;
	const Vector2 mouse = rect.coordinateSpace == InputViewCoordinateSpace::Screen ?
		mouseScreenPos_ : mousePos_;
	return (mouse.x >= rect.dstPos.x && mouse.y >= rect.dstPos.y &&
		mouse.x < rect.dstPos.x + rect.dstSize.x &&
		mouse.y < rect.dstPos.y + rect.dstSize.y);
}

std::optional<Vector2> Input::GetMousePosInView(InputViewArea viewArea) const {

	if (!IsMouseOnView(viewArea)) {
		return std::nullopt;
	}

	ViewRect rect = viewRects_.at(viewArea);
	if (rect.dstSize.x <= 0.0f || rect.dstSize.y <= 0.0f) {
		return std::nullopt;
	}
	const Vector2 mouse = rect.coordinateSpace == InputViewCoordinateSpace::Screen ?
		mouseScreenPos_ : mousePos_;
	Vector2 local = Vector2(mouse.x - rect.dstPos.x, mouse.y - rect.dstPos.y);
	if (rect.srcSize.x <= 0.0f || rect.srcSize.y <= 0.0f) {
		return local;
	}
	return local * (rect.srcSize / rect.dstSize);
}

Vector2 Input::GetMouseMoveValueInView(InputViewArea viewArea) const {

	const auto it = viewRects_.find(viewArea);
	if (it == viewRects_.end()) {
		return GetMouseMoveValue();
	}

	const ViewRect& rect = it->second;
	if (rect.dstSize.x <= 0.0f || rect.dstSize.y <= 0.0f ||
		rect.srcSize.x <= 0.0f || rect.srcSize.y <= 0.0f) {
		return GetMouseMoveValue();
	}
	return GetMouseMoveValue() * (rect.srcSize / rect.dstSize);
}

uint32_t Engine::Input::PlayVibration(const InputVibrationParams& params) {

	// 再生時間が正でなければ無効
	if (params.duration <= 0.0f) {
		return 0;
	}
	float l = std::clamp(params.left, 0.0f, 1.0f);
	float r = std::clamp(params.right, 0.0f, 1.0f);
	if (l <= 0.0f && r <= 0.0f) {
		return 0;
	}

	VibrationEffect e{};
	e.handle = nextVibHandle_++;
	if (nextVibHandle_ == 0) { nextVibHandle_ = 1; }

	e.left = l;
	e.right = r;
	e.duration = params.duration;
	e.attack = (std::max)(0.0f, params.attack);
	e.release = (std::max)(0.0f, params.release);
	e.priority = params.priority;
	e.start = std::chrono::steady_clock::now();

	vibEffects_.push_back(e);
	return e.handle;
}

void Engine::Input::StopVibration(uint32_t handle) {

	if (handle == 0) {
		return;
	}

	auto it = std::remove_if(vibEffects_.begin(), vibEffects_.end(),
		[&](const VibrationEffect& e) { return e.handle == handle; });
	vibEffects_.erase(it, vibEffects_.end());

	if (vibEffects_.empty()) {
		ApplyVibration(0, 0);
	}
}

void Engine::Input::StopAllVibration() {

	vibEffects_.clear();
	ApplyVibration(0, 0);
}

void Engine::Input::SetVibrationEnabled(bool enabled) {

	vibrationEnabled_ = enabled;
	if (!vibrationEnabled_) {
		StopAllVibration();
	}
}

Input* Input::GetInstance() {

	if (instance_ == nullptr) {
		instance_ = new Input();
	}
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

	return key_[keyNumber];
}

bool Input::TriggerKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

	// 現在のフレームで押されていて、前のフレームで押されていなかった場合にtrueを返す
	return key_[keyNumber] && !keyPre_[keyNumber];
}
bool Input::ReleaseKey(BYTE keyNumber, [[maybe_unused]] const std::source_location& location) {

	return !key_[keyNumber] && keyPre_[keyNumber];
}
bool Input::PushGamepadButton(GamePadButtons button, [[maybe_unused]] const std::source_location& location) {

	const size_t index = static_cast<size_t>(button);
	if (gamepadButtons_.size() <= index) {
		Assert::Call(false, "GamePad Button番号が範囲外です");
		return false;
	}
	return gamepadButtons_[index];
}
bool Input::TriggerGamepadButton(GamePadButtons button, [[maybe_unused]] const std::source_location& location) {

	// ボタン番号が範囲外の場合はfalseを返す
	if (gamepadButtons_.size() <= static_cast<size_t>(button)) {
		return false;
	}

	return gamepadButtons_[static_cast<size_t>(button)] &&
		!gamepadButtonsPre_[static_cast<size_t>(button)];
}
float Input::GetLeftTriggerValue() const {

	return leftTriggerValue_;
}
float Input::GetRightTriggerValue() const {

	return rightTriggerValue_;
}
Vector2 Input::GetLeftStickVal() const {
	return { leftThumbX_,leftThumbY_ };
}
Vector2 Input::GetRightStickVal() const {
	return { rightThumbX_,rightThumbY_ };
}
float Input::ApplyDeadZone(float value) {

	if (std::fabs(value) < deadZone_) {
		return 0.0f;
	}
	return value;
}
Vector2 Input::GetMousePos() const {

	return mousePos_;
}
Vector2 Input::GetMousePrePos() const {

	return mousePrePos_;
}
Vector2 Input::GetMouseMoveValue() const {

	return { static_cast<float>(mouseState_.lX),static_cast<float>(mouseState_.lY) };
}
float Input::GetMouseWheel() {

	return wheelValue_;
}
bool Input::PushMouseButton(size_t index, [[maybe_unused]] const std::source_location& location) const {

	Assert::Call(index < mouseButtons_.size(), "Mouse Button番号が範囲外です");
	return index < mouseButtons_.size() && mouseButtons_[index];
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

	return !mousePreButtons_[0] && mouseButtons_[0];
}
bool Input::TriggerMouseRight([[maybe_unused]] const std::source_location& location) const {

	return !mousePreButtons_[1] && mouseButtons_[1];
}
bool Input::TriggerMouseCenter([[maybe_unused]] const std::source_location& location) const {

	return !mousePreButtons_[2] && mouseButtons_[2];
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

		released = !mouseButtons_[0] && mousePreButtons_[0];
		break;
	}
	case MouseButton::Right: {

		released = !mouseButtons_[1] && mousePreButtons_[1];
		break;
	}
	case MouseButton::Center: {

		released = !mouseButtons_[2] && mousePreButtons_[2];
		break;
	}
	}
	return released;
}
void Input::SetDeadZone(float deadZone) {

	deadZone_ = std::clamp(deadZone, 0.0f, maxStickValue_);
}

void Input::Init(WinApp* winApp) {

	winApp_ = winApp;

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
	hr = keyboard_->SetCooperativeLevel(winApp_->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	Assert::Call(SUCCEEDED(hr), "キーボードの協調レベル設定に失敗しました");

	// マウスデバイスの初期化
	hr = dInput_->CreateDevice(GUID_SysMouse, &mouse_, NULL);
	Assert::Call(SUCCEEDED(hr), "マウス入力デバイスの作成に失敗しました");

	// 入力データ形式のセット
	hr = mouse_->SetDataFormat(&c_dfDIMouse);
	Assert::Call(SUCCEEDED(hr), "マウス入力形式の設定に失敗しました");

	// 排他制御レベルのリセット
	hr = mouse_->SetCooperativeLevel(winApp_->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	Assert::Call(SUCCEEDED(hr), "マウスの協調レベル設定に失敗しました");

	// マウスの取得開始
	hr = mouse_->Acquire();

	// 保存済み設定があれば上書きする
	LoadConfig();
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
	for (size_t i = 0; i < key_.size(); ++i) {
		if (key_[i] && !keyPre_[i]) {
			return true;
		}
	}

	// マウスボタンが押されたフレームをPC操作として扱う
	for (size_t i = 0; i < mouseButtons_.size(); ++i) {
		if (mouseButtons_[i] && !mousePreButtons_[i]) {
			return true;
		}
	}

	// 微小な揺れを除いたマウス移動とホイール操作を検出する
	constexpr float kMouseMoveThreshold = 2.0f;
	const Vector2 move = GetMouseMoveValue();
	if (std::sqrt(move.x * move.x + move.y * move.y) > kMouseMoveThreshold) {
		return true;
	}
	return wheelValue_ != 0.0f;
}

bool Input::HasGamepadInput() const {

	// 接続状態では切り替えず、ボタンとアナログ入力の開始だけを操作として扱う
	for (int i = 0; i < kMaxGamepads; ++i) {
		const size_t index = static_cast<size_t>(i);
		if (!padConnected_[index]) {
			continue;
		}

		const XINPUT_GAMEPAD& current = pads_[index].Gamepad;
		const XINPUT_GAMEPAD previous = padConnectedPre_[index]
			? padsPre_[index].Gamepad : XINPUT_GAMEPAD{};
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

void Input::Update() {

	mousePrePos_ = mousePos_;
	mousePreButtons_ = mouseButtons_;

	HRESULT hr;

	// キーボード情報の取得開始
	hr = keyboard_->Acquire();

	// 前回のキー入力を保存
	std::memcpy(keyPre_.data(), key_.data(), key_.size());

	// 全キーの入力状態を取得する
	hr = keyboard_->GetDeviceState(static_cast<DWORD>(key_.size()), key_.data());

	// 前回のゲームパッドの状態を保存
	std::memcpy(gamepadButtonsPre_.data(), gamepadButtons_.data(), gamepadButtons_.size());

	// gameplay用の多gamepad snapshotを更新する、indexはC# GamepadButton / GamepadAxis enumに対応する
	padsPre_ = pads_;
	padConnectedPre_ = padConnected_;
	for (int i = 0; i < kMaxGamepads; ++i) {
		ZeroMemory(&pads_[i], sizeof(XINPUT_STATE));
		padConnected_[i] = (XInputGetState(static_cast<DWORD>(i), &pads_[i]) == ERROR_SUCCESS);
	}

	// 既存single-gamepad pathはindex0のsnapshotを共有し、XInputGetStateの二重ポーリングを避ける
	gamepadState_ = pads_[0];
	gamepadConnected_ = padConnected_[0];

	if (gamepadConnected_) {

#pragma region ///ゲームパッドが接続されている場合の処理 ///
		gamepadButtons_[static_cast<size_t>(GamePadButtons::ARROW_UP)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::ARROW_DOWN)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::ARROW_LEFT)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::ARROW_RIGHT)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::START)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_START) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::BACK)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_BACK) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::LEFT_THUMB)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::RIGHT_THUMB)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::LEFT_SHOULDER)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::RIGHT_SHOULDER)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::LEFT_TRIGGER)] = (leftTriggerValue_ > 0);
		gamepadButtons_[static_cast<size_t>(GamePadButtons::RIGHT_TRIGGER)] = (rightTriggerValue_ > 0);
		gamepadButtons_[static_cast<size_t>(GamePadButtons::A)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::B)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::X)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_X) != 0;
		gamepadButtons_[static_cast<size_t>(GamePadButtons::Y)] = (gamepadState_.Gamepad.wButtons & XINPUT_GAMEPAD_Y) != 0;

		// スティックの状態を更新
		leftThumbX_ = ApplyDeadZone(gamepadState_.Gamepad.sThumbLX);
		leftThumbY_ = ApplyDeadZone(gamepadState_.Gamepad.sThumbLY);
		rightThumbX_ = ApplyDeadZone(gamepadState_.Gamepad.sThumbRX);
		rightThumbY_ = ApplyDeadZone(gamepadState_.Gamepad.sThumbRY);

		leftTriggerValue_ = static_cast<float>(gamepadState_.Gamepad.bLeftTrigger) / 255.0f;
		rightTriggerValue_ = static_cast<float>(gamepadState_.Gamepad.bRightTrigger) / 255.0f;
#pragma endregion
	} else {

		// ゲームパッドが接続されていない場合の処理
		std::fill(gamepadButtons_.begin(), gamepadButtons_.end(), false);

		leftThumbX_ = 0.0f;
		leftThumbY_ = 0.0f;
		rightThumbX_ = 0.0f;
		rightThumbY_ = 0.0f;

		leftTriggerValue_ = 0.0f;
		rightTriggerValue_ = 0.0f;
	}

	// 文字入力を確定し直前のmessage pumpで溜めたWM_CHAR分をframe-localテキストにする
	if (!pendingWide_.empty()) {
		const int needed = ::WideCharToMultiByte(CP_UTF8, 0, pendingWide_.c_str(),
			static_cast<int>(pendingWide_.size()), nullptr, 0, nullptr, nullptr);
		if (needed > 0) {
			frameText_.resize(static_cast<size_t>(needed));
			::WideCharToMultiByte(CP_UTF8, 0, pendingWide_.c_str(), static_cast<int>(pendingWide_.size()),
				frameText_.data(), needed, nullptr, nullptr);
		} else {
			frameText_.clear();
		}
		pendingWide_.clear();
	} else {
		frameText_.clear();
	}

	// デバイス振動の更新
	UpdateVibration();

	// マウス情報の取得開始
	hr = mouse_->Acquire();
	if (FAILED(hr)) {
		if (hr == DIERR_INPUTLOST || hr == DIERR_NOTACQUIRED) {

			mouse_->Acquire();
		}
	}

	hr = mouse_->GetDeviceState(sizeof(DIMOUSESTATE), &mouseState_);

	POINT screenPoint{};
	if (GetCursorPos(&screenPoint)) {
		mouseScreenPos_.x = static_cast<float>(screenPoint.x);
		mouseScreenPos_.y = static_cast<float>(screenPoint.y);

		POINT clientPoint = screenPoint;
		ScreenToClient(winApp_->GetHwnd(), &clientPoint);
		mousePos_.x = static_cast<float>(clientPoint.x);
		mousePos_.y = static_cast<float>(clientPoint.y);
	}

	if (FAILED(hr)) {
		// 取得失敗時の処理
		ZeroMemory(&mouseState_, sizeof(DIMOUSESTATE));
		std::fill(mouseButtons_.begin(), mouseButtons_.end(), false);
		wheelValue_ = 0.0f;
	} else {

		// マウスボタンの状態を保存
		mouseButtons_[0] = (mouseState_.rgbButtons[0] & 0x80) != 0;
		mouseButtons_[1] = (mouseState_.rgbButtons[1] & 0x80) != 0;
		mouseButtons_[2] = (mouseState_.rgbButtons[2] & 0x80) != 0;

		// ホイール値
		wheelValue_ = static_cast<float>(mouseState_.lZ) / WHEEL_DELTA;
	}
}

void Input::UpdateVibration() {

	// disabled -> always stop
	if (!vibrationEnabled_) {
		if (!vibEffects_.empty() || lastMotorLeft_ != 0 || lastMotorRight_ != 0) {
			vibEffects_.clear();
			ApplyVibration(0, 0);
		}
		return;
	}

	// disconnected -> clear & stop
	if (!gamepadConnected_) {
		if (!vibEffects_.empty() || lastMotorLeft_ != 0 || lastMotorRight_ != 0) {

			vibEffects_.clear();
			ApplyVibration(0, 0);
		}
		return;
	}

	if (vibEffects_.empty()) {
		// モーターを確実に停止する
		ApplyVibration(0, 0);
		return;
	}

	const auto now = std::chrono::steady_clock::now();

	// remove expired
	auto it = std::remove_if(vibEffects_.begin(), vibEffects_.end(), [&](const VibrationEffect& e) {
		const float t = std::chrono::duration<float>(now - e.start).count();
		return (t >= e.duration);
		});
	vibEffects_.erase(it, vibEffects_.end());

	float outL = 0.0f;
	float outR = 0.0f;

	for (const auto& e : vibEffects_) {
		const float t = std::chrono::duration<float>(now - e.start).count();
		const float remain = (std::max)(0.0f, e.duration - t);

		float gain = 1.0f;
		if (e.attack > 0.0f && t < e.attack) {
			gain = (std::min)(gain, t / e.attack);
		}
		if (e.release > 0.0f && remain < e.release) {
			gain = (std::min)(gain, remain / e.release);
		}

		outL = (std::max)(outL, e.left * gain);
		outR = ((std::max))(outR, e.right * gain);
	}

	// 振動を適用
	ApplyVibration(ToMotorSpeed(outL), ToMotorSpeed(outR));
}

void Input::ApplyVibration(uint16_t left, uint16_t right) {

	// 同じ値なら呼び出しを省く
	if (left == lastMotorLeft_ && right == lastMotorRight_) {
		return;
	}
	lastMotorLeft_ = left;
	lastMotorRight_ = right;

	XINPUT_VIBRATION vib{};
	vib.wLeftMotorSpeed = left;
	vib.wRightMotorSpeed = right;
	XInputSetState(0, &vib);
}

uint16_t Input::ToMotorSpeed(float v01) {

	v01 = std::clamp(v01, 0.0f, 1.0f);
	return static_cast<uint16_t>(v01 * 65535.0f + 0.5f);
}

//============================================================================
//	gameplay向け多gamepad / text / focus
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
	return index >= 0 && index < kMaxGamepads && padConnected_[static_cast<size_t>(index)];
}

int Input::ConnectedGamepadCount() const {
	int count = 0;
	for (int i = 0; i < kMaxGamepads; ++i) {
		if (padConnected_[static_cast<size_t>(i)]) {
			++count;
		}
	}
	return count;
}

bool Input::GamepadButtonByIndex(int index, int button) const {
	if (!GamepadConnectedByIndex(index)) {
		return false;
	}
	return IsGamepadButtonPressed(pads_[static_cast<size_t>(index)], button);
}

bool Input::GamepadButtonDownByIndex(int index, int button) const {
	if (index < 0 || index >= kMaxGamepads) {
		return false;
	}
	const bool now = padConnected_[static_cast<size_t>(index)] && IsGamepadButtonPressed(pads_[static_cast<size_t>(index)], button);
	const bool pre = padConnectedPre_[static_cast<size_t>(index)] && IsGamepadButtonPressed(padsPre_[static_cast<size_t>(index)], button);
	return now && !pre;
}

bool Input::GamepadButtonUpByIndex(int index, int button) const {
	if (index < 0 || index >= kMaxGamepads) {
		return false;
	}
	const bool now = padConnected_[static_cast<size_t>(index)] && IsGamepadButtonPressed(pads_[static_cast<size_t>(index)], button);
	const bool pre = padConnectedPre_[static_cast<size_t>(index)] && IsGamepadButtonPressed(padsPre_[static_cast<size_t>(index)], button);
	return pre && !now;
}

float Input::GamepadAxisByIndex(int index, int axis) const {
	if (!GamepadConnectedByIndex(index)) {
		return 0.0f;
	}
	// raw値を返しdead zoneはAction Map側で適用する、stickは[-1,1]でトリガは[0,1]
	const XINPUT_GAMEPAD& pad = pads_[static_cast<size_t>(index)].Gamepad;
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

//============================================================================
//	InputSystem classMethods
//============================================================================

namespace Engine {

	void Input::PushDroppedFiles(const std::vector<std::string>& paths, const Vector2& screenPoint) {

		if (paths.empty()) { return; }
		droppedFiles_ = paths;
		droppedFilesPoint_ = screenPoint;
		hasDroppedFiles_ = true;
	}

	bool Input::TakeDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) {

		if (!hasDroppedFiles_) { return false; }
		outPaths = std::move(droppedFiles_);
		outClientPoint = droppedFilesPoint_;
		droppedFiles_.clear();
		hasDroppedFiles_ = false;
		return true;
	}

	bool Input::PeekDroppedFiles(std::vector<std::string>& outPaths, Vector2& outClientPoint) const {

		if (!hasDroppedFiles_) { return false; }
		outPaths = droppedFiles_;
		outClientPoint = droppedFilesPoint_;
		return true;
	}
}
