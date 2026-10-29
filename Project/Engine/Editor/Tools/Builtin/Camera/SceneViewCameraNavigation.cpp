#include "SceneViewCameraNavigation.h"

//============================================================================
//	include
//============================================================================
// c++
#include <array>

using namespace Engine;

namespace {

	using Action = SceneViewCameraAction;
	using Source = SceneViewCameraControlSource;
	using InputState = SceneViewCameraInput;

	// 同じボタンでもマウス移動とキー移動は別の入力として扱う
	enum class Gesture { Drag, MoveKeys, Wheel };
	struct Binding {

		Action action;
		Gesture gesture;
		uint8_t buttons;
		uint8_t modifiers;
	};
	struct Bindings {

		std::array<Binding, 4> values{};
		size_t count = 0;
	};

	// UIの重複判定と実際の入力で同じ割当を使う
	Bindings GetBindings(Action action, Source source) {

		Bindings result;
		auto add = [&](Gesture gesture, uint8_t buttons, uint8_t modifiers = 0) {
			result.values[result.count++] = {action, gesture, buttons, modifiers};
		};
		if (source == Source::Disabled) { return result; }
		switch (action) {
		case Action::Move:
			if (source == Source::UE || source == Source::Unity) {
				add(Gesture::MoveKeys, InputState::kRight);
				add(Gesture::MoveKeys, InputState::kRight, InputState::kShift);
				add(Gesture::Wheel, InputState::kRight);
				add(Gesture::Wheel, InputState::kRight, InputState::kShift);
			}
			break;
		case Action::Pan:
			if (source == Source::UE || source == Source::Unity) { add(Gesture::Drag, InputState::kMiddle); }
			if (source == Source::UE || source == Source::Maya) { add(Gesture::Drag, InputState::kMiddle, InputState::kAlt); }
			if (source == Source::Blender) { add(Gesture::Drag, InputState::kMiddle, InputState::kShift); }
			break;
		case Action::Look:
			if (source == Source::UE || source == Source::Unity) {
				add(Gesture::Drag, InputState::kRight);
				add(Gesture::Drag, InputState::kRight, InputState::kShift);
			}
			break;
		case Action::Orbit:
			add(Gesture::Drag, source == Source::Blender ? InputState::kMiddle : InputState::kLeft,
				source == Source::Blender ? 0 : InputState::kAlt);
			break;
		case Action::Zoom:
			add(Gesture::Wheel, 0);
			add(Gesture::Drag, source == Source::Blender ? InputState::kMiddle : InputState::kRight,
				source == Source::Blender ? InputState::kControl : InputState::kAlt);
			break;
		}
		return result;
	}
}

SceneViewCameraControls Engine::SceneViewCameraNavigationUtility::ResolveControls(
	SceneViewCameraControlStyle style, const SceneViewCameraControls& custom) {

	if (style == SceneViewCameraControlStyle::Original) { return custom; }
	Source source = Source::Unity;
	switch (style) {
	case SceneViewCameraControlStyle::UE: source = Source::UE; break;
	case SceneViewCameraControlStyle::Blender: source = Source::Blender; break;
	case SceneViewCameraControlStyle::Maya: source = Source::Maya; break;
	default: break;
	}
	SceneViewCameraControls result;
	for (size_t index = 0; index < result.size(); ++index) {
		result[index] = SupportsAction(static_cast<Action>(index), source) ? source : Source::Disabled;
	}
	return result;
}

bool Engine::SceneViewCameraNavigationUtility::SupportsAction(Action action, Source source) {

	return source == Source::Disabled || GetBindings(action, source).count > 0;
}

std::optional<SceneViewCameraControlConflict> Engine::SceneViewCameraNavigationUtility::FindConflict(
	const SceneViewCameraControls& controls) {

	for (size_t first = 0; first < controls.size(); ++first) {
		const Bindings a = GetBindings(static_cast<Action>(first), controls[first]);
		for (size_t second = first + 1; second < controls.size(); ++second) {
			const Bindings b = GetBindings(static_cast<Action>(second), controls[second]);
			for (size_t i = 0; i < a.count; ++i) {
				for (size_t j = 0; j < b.count; ++j) {
					if (a.values[i].gesture == b.values[j].gesture && a.values[i].buttons == b.values[j].buttons &&
						a.values[i].modifiers == b.values[j].modifiers) {
						return SceneViewCameraControlConflict{static_cast<Action>(first), static_cast<Action>(second)};
					}
				}
			}
		}
	}
	return std::nullopt;
}

const char* Engine::SceneViewCameraNavigationUtility::GetActionLabel(Action action) {

	switch (action) {
	case Action::Move: return "キーボード移動";
	case Action::Pan: return "パン";
	case Action::Look: return "視点回転";
	case Action::Orbit: return "周回回転";
	case Action::Zoom: return "ズーム";
	}
	return "";
}

const char* Engine::SceneViewCameraNavigationUtility::GetControlDescription(Action action, Source source) {

	if (!SupportsAction(action, source) || source == Source::Disabled) { return "使用しない"; }
	switch (action) {
	case Action::Move: return "右ボタン + WASD / QE、Shiftで加速、ホイールで速度変更";
	case Action::Look: return "右ドラッグ";
	case Action::Pan:
		if (source == Source::Blender) { return "Shift + 中ドラッグ"; }
		if (source == Source::Maya) { return "Alt + 中ドラッグ"; }
		return source == Source::UE ? "中ドラッグ / Alt + 中ドラッグ" : "中ドラッグ";
	case Action::Orbit: return source == Source::Blender ? "中ドラッグ" : "Alt + 左ドラッグ";
	case Action::Zoom: return source == Source::Blender ? "ホイール / Ctrl + 中ドラッグ" : "ホイール / Alt + 右ドラッグ";
	}
	return "";
}

SceneViewCameraNavigationCommand Engine::SceneViewCameraNavigationSession::Resolve(
	const SceneViewCameraControls& controls, const SceneViewCameraInput& input) {

	SceneViewCameraNavigationCommand command;
	if (!input.available) {
		Cancel();
		return command;
	}
	// ビュー外で押したボタンを途中から操作に使わない
	capturedButtons_ &= input.buttons;
	if (input.pressedButtons != 0) { capturedButtons_ |= input.pressedButtons; }
	if (input.buttons == 0) { ownsGesture_ = false; }
	for (size_t index = 0; index < controls.size(); ++index) {
		const Bindings bindings = GetBindings(static_cast<Action>(index), controls[index]);
		for (size_t i = 0; i < bindings.count; ++i) {
			const Binding& binding = bindings.values[i];
			if (binding.buttons != input.buttons || binding.modifiers != input.modifiers ||
				(binding.buttons != 0 && (capturedButtons_ & binding.buttons) != binding.buttons)) { continue; }
			if (binding.gesture == Gesture::Wheel) {
				if (input.wheel == 0.0f) { continue; }
				if (binding.action == Action::Move) { command.speedWheel = input.wheel; }
				else { command.zoomWheel = input.wheel; }
			} else if (binding.gesture == Gesture::MoveKeys) {
				command.movement = input.movement;
				command.accelerated = (input.modifiers & InputState::kShift) != 0;
			} else {
				switch (binding.action) {
				case Action::Pan: command.pan = true; break;
				case Action::Look: command.look = true; break;
				case Action::Orbit: command.orbit = true; break;
				case Action::Zoom: command.zoomDrag = true; break;
				default: break;
				}
			}
			command.active = true;
		}
	}
	// 修飾キーを離してもボタンを離すまでは選択へ渡さない
	if (command.active && capturedButtons_ != 0) { ownsGesture_ = true; }
	active_ = command.active || ownsGesture_;
	command.active = active_;
	return command;
}

void Engine::SceneViewCameraNavigationSession::Cancel() {

	capturedButtons_ = 0;
	ownsGesture_ = false;
	active_ = false;
}
