#include "InputVibrationPlayer.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
// windows
#include <Windows.h>
#include <XInput.h>

uint32_t InputVibrationPlayer::PlayVibration(const InputVibrationParams& params) {

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

void InputVibrationPlayer::StopVibration(uint32_t handle) {

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

void InputVibrationPlayer::StopAllVibration() {

	vibEffects_.clear();
	ApplyVibration(0, 0);
}

void InputVibrationPlayer::SetVibrationEnabled(bool enabled) {

	vibrationEnabled_ = enabled;
	if (!vibrationEnabled_) {
		StopAllVibration();
	}
}

void InputVibrationPlayer::UpdateVibration(bool gamepadConnected) {

	// disabled -> always stop
	if (!vibrationEnabled_) {
		if (!vibEffects_.empty() || lastMotorLeft_ != 0 || lastMotorRight_ != 0) {
			vibEffects_.clear();
			ApplyVibration(0, 0);
		}
		return;
	}

	// disconnected -> clear & stop
	if (!gamepadConnected) {
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

void InputVibrationPlayer::ApplyVibration(uint16_t left, uint16_t right) {

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

uint16_t InputVibrationPlayer::ToMotorSpeed(float v01) {

	v01 = std::clamp(v01, 0.0f, 1.0f);
	return static_cast<uint16_t>(v01 * 65535.0f + 0.5f);
}
