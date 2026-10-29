#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraSettings.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <cmath>
#include <limits>

using namespace Engine;
using namespace Engine::SceneViewCameraNavigationUtility;

namespace {

	using Style = SceneViewCameraControlStyle;
	using Source = SceneViewCameraControlSource;
	using Action = SceneViewCameraAction;
	using Snapshot = SceneViewCameraInput;

	// ビュー内で入力を開始する
	SceneViewCameraInput Press(uint8_t buttons, uint8_t modifiers = 0) {

		SceneViewCameraInput input;
		input.available = true;
		input.buttons = buttons;
		input.pressedButtons = buttons;
		input.modifiers = modifiers;
		input.mouseDelta = Vector2(4.0f, 3.0f);
		input.deltaTime = 1.0f / 60.0f;
		return input;
	}

	// カメラを軸に沿わせて移動量を確認する
	SceneViewCameraSettings AxisCamera() {

		SceneViewCameraSettings settings;
		settings.cameraState.transform3D.pos = Vector3::AnyInit(0.0f);
		settings.cameraState.transform3D.rotation = Vector3::AnyInit(0.0f);
		return settings;
	}

	// 全方式で主要な操作が独立して解決されるか確認する
	bool CheckPresets() {

		for (const auto style : {Style::UE, Style::Unity, Style::Blender, Style::Maya}) {
			const auto controls = ResolveControls(style, {});
			if (FindConflict(controls)) { return false; }
			SceneViewCameraNavigationSession session;
			const auto orbit = session.Resolve(controls, Press(style == Style::Blender ? Snapshot::kMiddle : Snapshot::kLeft,
				style == Style::Blender ? 0 : Snapshot::kAlt));
			if (!orbit.orbit || orbit.pan || orbit.look || orbit.zoomDrag) { return false; }
			session.Cancel();
			const auto pan = session.Resolve(controls, Press(Snapshot::kMiddle,
				style == Style::Blender ? Snapshot::kShift : style == Style::Maya ? Snapshot::kAlt : 0));
			if (!pan.pan || pan.orbit || pan.look || pan.zoomDrag) { return false; }
			session.Cancel();
			const auto zoom = session.Resolve(controls, Press(style == Style::Blender ? Snapshot::kMiddle : Snapshot::kRight,
				style == Style::Blender ? Snapshot::kControl : Snapshot::kAlt));
			if (!zoom.zoomDrag || zoom.pan || zoom.look || zoom.orbit) { return false; }
			session.Cancel();
			auto input = Press(Snapshot::kRight);
			input.movement = Vector3(0.0f, 0.0f, 1.0f);
			input.wheel = 1.0f;
			const auto fly = session.Resolve(controls, input);
			const bool flySupported = style == Style::UE || style == Style::Unity;
			if (fly.look != flySupported || (fly.movement.z == 1.0f) != flySupported ||
				(fly.speedWheel == 1.0f) != flySupported || fly.zoomWheel != 0.0f) { return false; }
		}
		return true;
	}

	// 外で押したボタンとフォーカスを失った操作を再開しない
	bool CheckInputLifetime() {

		const auto controls = ResolveControls(Style::Unity, {});
		SceneViewCameraNavigationSession session;
		auto input = Press(Snapshot::kRight);
		input.available = false;
		if (session.Resolve(controls, input).active) { return false; }
		input.available = true;
		input.pressedButtons = 0;
		if (session.Resolve(controls, input).active) { return false; }
		input.pressedButtons = Snapshot::kRight;
		if (!session.Resolve(controls, input).look) { return false; }
		input.pressedButtons = 0;
		input.available = false;
		if (session.Resolve(controls, input).active || session.IsActive()) { return false; }
		input.available = true;
		if (session.Resolve(controls, input).active) { return false; }
		input = Press(Snapshot::kLeft, Snapshot::kAlt);
		if (!session.Resolve(controls, input).orbit) { return false; }
		input.modifiers = 0;
		input.pressedButtons = 0;
		if (!session.Resolve(controls, input).active) { return false; }
		input.buttons = 0;
		if (session.Resolve(controls, input).active || session.IsActive()) { return false; }
		session.Resolve(controls, Press(Snapshot::kRight));
		session.Cancel();
		input = Press(Snapshot::kRight);
		input.pressedButtons = 0;
		return !session.Resolve(controls, input).active;
	}

	// 全組合せについて重複判定と実際の入力を照合する
	bool CheckConflicts() {

		for (uint32_t encoded = 0; encoded < 3125; ++encoded) {
			SceneViewCameraControls controls;
			uint32_t remaining = encoded;
			for (auto& source : controls) { source = static_cast<Source>(remaining % 5); remaining /= 5; }
			const bool conflict = FindConflict(controls).has_value();
			bool observedConflict = false;
			for (uint8_t buttons = 1; buttons < 8; ++buttons) {
				for (uint8_t modifiers = 0; modifiers < 8; ++modifiers) {
					SceneViewCameraNavigationSession session;
					const auto command = session.Resolve(controls, Press(buttons, modifiers));
					const int dragActions = static_cast<int>(command.pan) + static_cast<int>(command.look) +
						static_cast<int>(command.orbit) + static_cast<int>(command.zoomDrag);
					observedConflict |= dragActions > 1;
				}
			}
			if (conflict != observedConflict) { return false; }
		}
		return true;
	}

	// 時間差と斜め入力に依存せず同じ速度で移動する
	bool CheckMovement() {

		auto advance = [](uint32_t frames, Vector3 movement, bool accelerate = false) {
			auto settings = AxisCamera();
			SceneViewCameraNavigationState state;
			SceneViewCameraNavigationCommand command;
			command.active = true;
			command.movement = movement;
			command.accelerated = accelerate;
			auto input = Press(0);
			input.deltaTime = 1.0f / static_cast<float>(frames);
			for (uint32_t frame = 0; frame < frames; ++frame) { ApplyMotion(settings, state, command, input); }
			return settings.cameraState.transform3D.pos;
		};
		const Vector3 thirty = advance(30, Vector3(0.0f, 0.0f, 1.0f));
		const Vector3 fast = advance(120, Vector3(0.0f, 0.0f, 1.0f));
		const Vector3 diagonal = advance(60, Vector3(1.0f, 0.0f, 1.0f));
		const Vector3 accelerated = advance(60, Vector3(0.0f, 0.0f, 1.0f), true);
		// 見上げた状態でも前進と上昇の合成速度を揃える
		auto settings = AxisCamera();
		settings.cameraState.transform3D.rotation.x = -45.0f;
		SceneViewCameraNavigationState state;
		SceneViewCameraNavigationCommand command;
		command.active = true;
		command.movement = Vector3(0.0f, 1.0f, 1.0f);
		auto input = Press(0);
		input.deltaTime = 1.0f;
		ApplyMotion(settings, state, command, input);
		return (thirty - fast).Length() < 0.001f && std::abs(thirty.z - 10.0f) < 0.001f &&
			std::abs(diagonal.Length() - thirty.Length()) < 0.001f && std::abs(accelerated.z - 40.0f) < 0.001f &&
			std::abs(settings.cameraState.transform3D.pos.Length() - thirty.Length()) < 0.001f;
	}

	// 回転とズームで中心と投影を壊さない
	bool CheckMotion() {

		auto settings = AxisCamera();
		SceneViewCameraNavigationState state{Vector3(0.0f, 0.0f, 20.0f), true};
		SceneViewCameraNavigationCommand command;
		command.active = true;
		command.orbit = true;
		auto input = Press(0);
		input.mouseDelta = Vector2(60.0f, 20.0f);
		ApplyMotion(settings, state, command, input);
		if (std::abs((settings.cameraState.transform3D.pos - state.pivot).Length() - 20.0f) > 0.001f ||
			state.pivot != Vector3(0.0f, 0.0f, 20.0f)) { return false; }
		const Vector3 position = settings.cameraState.transform3D.pos;
		command.orbit = false;
		command.look = true;
		ApplyMotion(settings, state, command, input);
		if (settings.cameraState.transform3D.pos != position) { return false; }
		command.look = false;
		command.pan = true;
		const Vector3 pivot = state.pivot;
		ApplyMotion(settings, state, command, input);
		const Vector3 panOffset = settings.cameraState.transform3D.pos - position;
		if (panOffset.Length() <= 0.0f || ((state.pivot - pivot) - panOffset).Length() > 0.0001f) { return false; }
		command.pan = false;
		command.zoomWheel = 1000000.0f;
		ApplyMotion(settings, state, command, input);
		if ((settings.cameraState.transform3D.pos - state.pivot).Length() < settings.cameraState.perspectiveNearClip ||
			settings.cameraState.perspectiveFovY != SceneViewCameraDefaults::kDefaultFovY) { return false; }
		const Vector3 validPosition = settings.cameraState.transform3D.pos;
		command.zoomWheel = 0.0f;
		command.look = true;
		input.mouseDelta.x = std::numeric_limits<float>::quiet_NaN();
		ApplyMotion(settings, state, command, input);
		if (settings.cameraState.transform3D.pos != validPosition || !SceneViewCameraSettingsUtility::IsFinite(state.pivot)) { return false; }

		// プレビューの回転前の軸と固定量ズームを維持する
		settings = AxisCamera();
		state = {};
		command = {};
		command.active = command.preview = command.pan = command.look = true;
		command.zoomWheel = 1.0f;
		input.mouseDelta = Vector2(10.0f, 5.0f);
		ApplyMotion(settings, state, command, input);
		return (settings.cameraState.transform3D.pos - Vector3(0.2f, -0.1f, 0.4f)).Length() < 0.0001f;
	}
}

bool NEMTests::TestSceneViewCameraNavigation() {

	return CheckPresets() && CheckInputLifetime() && CheckConflicts() && CheckMovement() && CheckMotion();
}
