#include "InputSystem.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Windows/Win32Window.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Runtime/Paths/ConfigPaths.h>

// imgui
#include <imgui.h>

#pragma comment(lib,"dInput8.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib, "xinput.lib")

//============================================================================
//	Input classMethods
//============================================================================
namespace {

	const char* kMouseNames[3] = { "MouseLeft", "MouseRight", "MouseCenter" };

	std::string MakeCallerTag(const std::source_location& location, std::string_view label) {

		std::string_view fn = location.function_name();

		auto pos = fn.find(' ');
		if (pos != std::string_view::npos) fn.remove_prefix(pos + 1);

		constexpr std::string_view cdeclStr = "__cdecl ";
		if (fn.starts_with(cdeclStr)) fn.remove_prefix(cdeclStr.size());

		std::string tag;
		tag.reserve(fn.size() + label.size() + 1);
		tag.append(fn);
		tag.push_back(':');
		tag.append(label);
		return tag;
	}

	const char* ToDikName(uint8_t dik) {

		switch (dik) {
		case DIK_W:   return "DIK_W";
		case DIK_A:   return "DIK_A";
		case DIK_S:   return "DIK_S";
		case DIK_D:   return "DIK_D";
		case DIK_R:   return "DIK_R";
		case DIK_E:   return "DIK_E";
		case DIK_Q:   return "DIK_Q";
		case DIK_UP:   return "DIK_UP";
		case DIK_DOWN:   return "DIK_DOWN";
		case DIK_RIGHT:   return "DIK_RIGHT";
		case DIK_LEFT:   return "DIK_LEFT";
		case DIK_F1: return "DIK_F1";
		case DIK_F2: return "DIK_F2";
		case DIK_F3: return "DIK_F3";
		case DIK_F10: return "DIK_F10";
		case DIK_F11: return "DIK_F11";
		case DIK_RETURN:   return "DIK_RETURN";
		case DIK_SPACE: return "DIK_SPACE";
		case DIK_ESCAPE: return "DIK_ESCAPE";
		default:      break;
		}
		static char buf[8];
		std::snprintf(buf, sizeof(buf), "0x%02X", dik);
		return buf;
	}

	template<size_t N, class TAG_FUNC, class NAME_FUNC>
	void LogEnterStayExit(bool now, bool prev,
		size_t idx, std::array<std::chrono::steady_clock::time_point, N>& start,
		std::array<bool, N>& stayDone, TAG_FUNC&& makeTag, NAME_FUNC&& toName) {

		if (now) {
			if (!prev) {

				start[idx] = std::chrono::steady_clock::now();
				stayDone[idx] = false;

				Logger::Output(LogType::Engine, "{}  Enter {}", makeTag(), toName());
			} else if (!stayDone[idx]) {

				auto dur = std::chrono::steady_clock::now() - start[idx];

				Logger::Output(LogType::Engine, "{}  Stay  {}  {:.1f}ms",
					makeTag(), toName(),
					std::chrono::duration<float, std::milli>(dur).count());

				stayDone[idx] = true;
			}
		} else if (prev) {

			auto dur = std::chrono::steady_clock::now() - start[idx];

			Logger::Output(LogType::Engine, "{}  Exit  {}  {:.1f}ms",
				makeTag(), toName(),
				std::chrono::duration<float, std::milli>(dur).count());
		}
	}
}

Input* Input::instance_ = nullptr;

void Input::SetViewRect(InputViewArea viewArea, const Vector2& dstPos,
	const Vector2& dstSize, const Vector2& srcSize) {

	ViewRect& rect = viewRects_[viewArea];
	rect.dstPos = dstPos;
	rect.dstSize = dstSize;
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

	Vector2 mouse = GetMousePos();
	const ViewRect rect = viewRects_.at(viewArea);
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
	Vector2 mouse = GetMousePos();
	Vector2 local = Vector2(mouse.x - rect.dstPos.x, mouse.y - rect.dstPos.y);
	if (rect.srcSize.x <= 0.0f || rect.srcSize.y <= 0.0f) {
		return local;
	}
	return local * (rect.srcSize / rect.dstSize);
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

bool Input::PushKey(BYTE keyNumber, const std::source_location& location) {

	const bool now = key_[keyNumber];
	const bool prev = keyPre_[keyNumber];

	if (now) {
		if (!prev) {
			keyStartTime_[keyNumber] = std::chrono::steady_clock::now();
			keyStayLogged_[keyNumber] = false;

			Logger::Output(LogType::Engine, "{}  Enter {}",
				MakeCallerTag(location, "Key"),
				ToDikName(keyNumber));
		} else if (!keyStayLogged_[keyNumber]) {

			auto dur = std::chrono::steady_clock::now() - keyStartTime_[keyNumber];

			Logger::Output(LogType::Engine, "{}  Stay  {}  {:.1f}ms",
				MakeCallerTag(location, "Key"),
				ToDikName(keyNumber),
				std::chrono::duration<float, std::milli>(dur).count());

			keyStayLogged_[keyNumber] = true;
		}
	} else if (prev) {

		auto dur = std::chrono::steady_clock::now() - keyStartTime_[keyNumber];

		Logger::Output(LogType::Engine, "{}  Exit  {}  {:.1f}ms",
			MakeCallerTag(location, "Key"),
			ToDikName(keyNumber),
			std::chrono::duration<float, std::milli>(dur).count());
	}

	return now;
}

bool Input::TriggerKey(BYTE keyNumber, const std::source_location& location) {

	// 現在のフレームで押されていて、前のフレームで押されていなかった場合にtrueを返す
	if (key_[keyNumber] && !keyPre_[keyNumber]) {

		Logger::Output(LogType::Engine, "{}", MakeCallerTag(location, "TriggerKey:" + std::string(ToDikName(keyNumber))));
		return true;
	}

	return false;
}
bool Input::ReleaseKey(BYTE keyNumber, const std::source_location& location) {

	if (!key_[keyNumber] && keyPre_[keyNumber]) {

		Logger::Output(LogType::Engine, "{}", MakeCallerTag(location, "ReleaseKey:" + std::string(ToDikName(keyNumber))));
		return true;
	}
	return false;
}
bool Input::PushGamepadButton(GamePadButtons button, const std::source_location& location) {

	const size_t index = static_cast<size_t>(button);
	const bool now = gamepadButtons_[index];
	const bool prev = gamepadButtonsPre_[index];

	LogEnterStayExit(now, prev, index,
		gpStartTime_, gpStayLogged_,
		[&] { return MakeCallerTag(location, "GamePadButtons:"); },
		[&] { return EnumAdapter<GamePadButtons>::ToString(button); });

	return now;
}
bool Input::TriggerGamepadButton(GamePadButtons button, const std::source_location& location) {

	// ボタン番号が範囲外の場合はfalseを返す
	if (gamepadButtons_.size() <= static_cast<size_t>(button)) {
		return false;
	}

	bool trigger = gamepadButtons_[static_cast<size_t>(button)] && !gamepadButtonsPre_[static_cast<size_t>(button)];
	if (trigger) {

		Logger::Output(LogType::Engine, "{}", MakeCallerTag(location,
			"TriggerGamepadButton:" + std::string(EnumAdapter<GamePadButtons>::ToString(button))));
	}

	return trigger;
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
bool Input::PushMouseButton(size_t index, const std::source_location& location) const {

	const bool now = mouseButtons_[index];
	const bool prev = mousePreButtons_[index];

	auto& start = const_cast<Input*>(this)->mouseStartTime_;
	auto& stayDone = const_cast<Input*>(this)->mouseStayLogged_;

	LogEnterStayExit(now, prev, index,
		start, stayDone,
		[&] { return MakeCallerTag(location, "Mouse"); },
		[&] { return kMouseNames[index]; });

	return now;
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
bool Input::TriggerMouseLeft(const std::source_location& location) const {

	bool trigger = !mousePreButtons_[0] && mouseButtons_[0];
	if (trigger) {

		Logger::Output(LogType::Engine, "{}  mouseLeft=triggered", MakeCallerTag(location, "TriggerMouseLeft"));
	}

	return trigger;
}
bool Input::TriggerMouseRight(const std::source_location& location) const {

	bool trigger = !mousePreButtons_[1] && mouseButtons_[1];
	if (trigger) {

		Logger::Output(LogType::Engine, "{}  mouseRight=triggered", MakeCallerTag(location, "TriggerMouseRight"));
	}

	return trigger;
}
bool Input::TriggerMouseCenter(const std::source_location& location) const {

	bool trigger = !mousePreButtons_[2] && mouseButtons_[2];
	if (trigger) {

		Logger::Output(LogType::Engine, "{}  mouseCenter=triggered", MakeCallerTag(location, "TriggerMouseCenter"));
	}

	return trigger;
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
bool Input::ReleaseMouse(MouseButton button, const std::source_location& location) const {

	bool released = false;
	switch (button) {
	case MouseButton::Left: {

		released = !mouseButtons_[0] && mousePreButtons_[0];
		if (released) {

			Logger::Output(LogType::Engine, "{}  mouseLeft=released", MakeCallerTag(location, "ReleaseMouseLeft"));
		}
		break;
	}
	case MouseButton::Right: {

		released = !mouseButtons_[1] && mousePreButtons_[1];
		if (released) {

			Logger::Output(LogType::Engine, "{}  mouseRight=released", MakeCallerTag(location, "ReleaseMouseRight"));
		}
		break;
	}
	case MouseButton::Center: {

		released = !mouseButtons_[2] && mousePreButtons_[2];
		if (released) {

			Logger::Output(LogType::Engine, "{}  mouseCenter=released", MakeCallerTag(location, "ReleaseMouseCenter"));
		}
		break;
	}
	}
	return released;
}
void Input::SetDeadZone(float deadZone) {

	deadZone_ = deadZone;
}

void Input::Init(WinApp* winApp) {

	winApp_ = winApp;

	HRESULT hr;

	// DirectInputの初期化
	dInput_ = nullptr;
	hr = DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&dInput_, nullptr);
	assert(SUCCEEDED(hr));

	// キーボードデバイスの初期化
	keyboard_ = nullptr;
	hr = dInput_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL);
	assert(SUCCEEDED(hr));

	// 入力データ形式のセット標準形式
	hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
	assert(SUCCEEDED(hr));

	// 排他制御レベルのリセット
	hr = keyboard_->SetCooperativeLevel(winApp_->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
	assert(SUCCEEDED(hr));

	// マウスデバイスの初期化
	hr = dInput_->CreateDevice(GUID_SysMouse, &mouse_, NULL);
	assert(SUCCEEDED(hr));

	// 入力データ形式のセット
	hr = mouse_->SetDataFormat(&c_dfDIMouse);
	assert(SUCCEEDED(hr));

	// 排他制御レベルのリセット
	hr = mouse_->SetCooperativeLevel(winApp_->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
	assert(SUCCEEDED(hr));

	// マウスの取得開始
	hr = mouse_->Acquire();

	// 入力タイプ切替の既定トリガ、スティック/マウス移動と主要ボタンで切り替える
	detectTriggers_ = {
		{ InputType::GamePad, true, 0 },
		{ InputType::GamePad, false, static_cast<int32_t>(GamePadButtons::A) },
		{ InputType::Keyboard, true, 0 },
	};
	// 保存済み設定があれば上書きする
	LoadConfig();
}

namespace {
	// 入力デバイス設定の保存先
	constexpr const char* kInputDeviceConfigPath = ConfigPaths::kInputDevice;
}

void Input::LoadConfig() {

	const std::filesystem::path path = RuntimePaths::GetGameConfigPath(kInputDeviceConfigPath);
	if (!JsonAdapter::Check(path)) {
		return;
	}
	const nlohmann::json data = JsonAdapter::Load(path);
	if (!data.is_object()) {
		return;
	}

	deadZone_ = data.value("deadZone", deadZone_);
	autoUpdateInputType_ = data.value("autoUpdateInputType", autoUpdateInputType_);
	movementThreshold_ = data.value("movementThreshold", movementThreshold_);

	// 検知トリガを復元する
	if (data.contains("detectTriggers") && data["detectTriggers"].is_array()) {
		detectTriggers_.clear();
		for (const nlohmann::json& node : data["detectTriggers"]) {
			InputDetectTrigger trigger{};
			trigger.device = static_cast<InputType>(node.value("device", 0));
			trigger.isMovement = node.value("isMovement", false);
			trigger.code = node.value("code", 0);
			detectTriggers_.push_back(trigger);
		}
	}

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
	data["autoUpdateInputType"] = autoUpdateInputType_;
	data["movementThreshold"] = movementThreshold_;

	nlohmann::json triggers = nlohmann::json::array();
	for (const InputDetectTrigger& trigger : detectTriggers_) {
		nlohmann::json node{};
		node["device"] = static_cast<int32_t>(trigger.device);
		node["isMovement"] = trigger.isMovement;
		node["code"] = trigger.code;
		triggers.push_back(node);
	}
	data["detectTriggers"] = triggers;

	data["mouseRangeControl"] = mouseRangeControl_;
	data["mouseAreaPosX"] = mouseAreaPos_.x;
	data["mouseAreaPosY"] = mouseAreaPos_.y;
	data["mouseAreaSizeX"] = mouseAreaSize_.x;
	data["mouseAreaSizeY"] = mouseAreaSize_.y;
	data["mouseReleaseModKey"] = mouseReleaseModKey_;
	data["mouseReleaseTriggerKey"] = mouseReleaseTriggerKey_;

	JsonAdapter::Save(RuntimePaths::GetGameConfigPath(kInputDeviceConfigPath), data);
}

void Input::UpdateInputDevice() {

	// マウス移動の判定しきい値、ピクセル単位
	constexpr float kMouseMoveThreshold = 2.0f;

	// 検知トリガから入力タイプを自動更新する、最初に成立したトリガのデバイスへ切り替える
	if (autoUpdateInputType_) {
		for (const InputDetectTrigger& trigger : detectTriggers_) {

			bool active = false;
			if (trigger.isMovement) {
				// 切替先がパッドなら右スティック、それ以外はマウス移動量で判定する
				if (trigger.device == InputType::GamePad) {
					const Vector2 stick = GetRightStickVal();
					const float magnitude = std::sqrt(stick.x * stick.x + stick.y * stick.y) / maxStickValue_;
					active = magnitude > movementThreshold_;
				} else {
					const Vector2 move = GetMouseMoveValue();
					active = std::sqrt(move.x * move.x + move.y * move.y) > kMouseMoveThreshold;
				}
			} else if (trigger.device == InputType::GamePad) {
				active = PushGamepadButton(static_cast<GamePadButtons>(trigger.code));
			} else {
				active = PushKey(static_cast<BYTE>(trigger.code));
			}
			if (active) {
				inputType_ = trigger.device;
				break;
			}
		}
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

	if (FAILED(hr)) {
		// 取得失敗時の処理
		std::fill(mouseButtons_.begin(), mouseButtons_.end(), false);
		mousePos_ = { 0.0f, 0.0f };
	} else {

		// マウスボタンの状態を保存
		mouseButtons_[0] = (mouseState_.rgbButtons[0] & 0x80) != 0;
		mouseButtons_[1] = (mouseState_.rgbButtons[1] & 0x80) != 0;
		mouseButtons_[2] = (mouseState_.rgbButtons[2] & 0x80) != 0;

		POINT point;
		GetCursorPos(&point);
		ScreenToClient(winApp_->GetHwnd(), &point);

		// マウスの移動量を保存
		mousePos_.x = static_cast<float>(point.x);
		mousePos_.y = static_cast<float>(point.y);

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
