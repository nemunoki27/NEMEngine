#include "InputConfigurationTests.h"
#include <Engine/Editor/Core/EditorShortcutState.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputDeviceConfiguration.h>
#include <Engine/Core/Platform/Input/InputDeviceState.h>
#include <Engine/Core/Platform/Input/InputWindowEvents.h>
#include <Engine/Core/Platform/Input/InputViewMapping.h>
#include <Engine/Core/Platform/Input/InputVibrationPlayer.h>

// c++
#include <array>
#include <limits>

namespace NEMTests {

	using namespace Engine;

	// 不正な保存設定で確定済みの入力設定を変更しない
	bool TestInputConfiguration() {

		// Window間の移動とアプリ外への移動と破棄を区別する
		InputWindowEvents windows;
		const auto main = reinterpret_cast<HWND>(1);
		const auto detached = reinterpret_cast<HWND>(2);
		windows.RegisterWindow(main);
		windows.RegisterWindow(detached);
		for (const auto hwnd : {main, detached, main, detached}) {
			windows.SetFocusedWindow(hwnd);
			if (!windows.HasWindowFocus() || windows.GetFocusedWindow() != hwnd) return false;
		}
		windows.UnregisterWindow(main);
		if (!windows.HasWindowFocus() || windows.GetFocusedWindow() != detached) return false;
		windows.SetFocusedWindow(main);
		if (windows.HasWindowFocus() || windows.GetFocusedWindow()) return false;
		windows.SetFocusedWindow(detached);
		windows.AppendTextInputUtf16(L'A');
		windows.UnregisterWindow(detached);
		windows.CommitText();
		if (windows.HasWindowFocus() || windows.GetFocusedWindow() || !windows.FrameTextInput().empty()) return false;
		windows.RegisterWindow(detached);
		windows.SetFocusedWindow(detached);
		if (!windows.HasWindowFocus()) return false;
		windows.SetFocusedWindow(nullptr);
		if (windows.HasWindowFocus()) return false;

		// 左側や上側のモニターでもView座標とメインWindow座標を混同しない
		InputViewMapping mapping;
		InputDeviceState pointer;
		pointer.mousePos = Vector2(100.0f, 50.0f);
		pointer.mouseScreenPos = Vector2(-1750.0f, -750.0f);
		for (const auto view : {InputViewArea::Scene, InputViewArea::Game}) {
			mapping.SetViewRect(view, Vector2(-1800.0f, -800.0f), Vector2(200.0f, 100.0f),
				Vector2(400.0f, 200.0f), InputViewCoordinateSpace::Screen);
			if (mapping.GetMousePosInView(view, pointer) != Vector2(100.0f, 100.0f)) return false;
			mapping.ClearViewRect(view);
			if (mapping.GetMousePosInView(view, pointer)) return false;
		}

		// 重いframeの間に解放した操作も一度だけ取り出す
		EditorShortcutState shortcut;
		shortcut.ProcessMessage(WM_KEYDOWN, VK_TAB, 0);
		shortcut.ProcessMessage(WM_KEYDOWN, VK_ESCAPE, 0);
		shortcut.ProcessMessage(WM_KEYDOWN, VK_ESCAPE, LPARAM{ 1 } << 30);
		shortcut.ProcessMessage(WM_KEYUP, VK_ESCAPE, 0);
		shortcut.ProcessMessage(WM_KEYUP, VK_TAB, 0);
		if (!shortcut.Consume() || shortcut.Consume()) return false;
		// 逆順と再入力とフォーカス解除を確認
		shortcut.ProcessMessage(WM_KEYDOWN, VK_ESCAPE, 0);
		shortcut.ProcessMessage(WM_KEYDOWN, VK_TAB, 0);
		if (!shortcut.Consume() || shortcut.Consume()) return false;
		shortcut.ProcessMessage(WM_KEYUP, VK_TAB, 0);
		shortcut.ProcessMessage(WM_KEYDOWN, VK_TAB, 0);
		shortcut.Reset();
		if (shortcut.Consume()) return false;
		shortcut.ProcessMessage(WM_KEYDOWN, VK_TAB, 0);
		if (shortcut.Consume()) return false;

		InputDeviceConfiguration configuration;
		configuration.deadZone = 12000.0f;
		configuration.backgroundInputEnabled = true;
		configuration.playerGamepads[0] = -1;
		configuration.mouseAreaSize = Vector2(640.0f, 480.0f);
		const auto saved = InputDeviceConfigurationSerialization::Write(configuration);
		InputDeviceConfiguration restored;
		if (!InputDeviceConfigurationSerialization::TryRead(saved, restored) ||
			InputDeviceConfigurationSerialization::Write(restored) != saved) {
			return false;
		}

		// 後半の項目で失敗しても前半の変更を残さない
		const std::array<nlohmann::json, 8> invalid{{
			{{"deadZone", 100.0f}, {"mouseReleaseTriggerKey", "Enter"}},
			{{"deadZone", 100.0f}, {"players", {3}}},
			{{"deadZone", 100.0f}, {"players", {{{"gamepad", UINT64_MAX}}}}},
			{{"deadZone", 100.0f}, {"players", {{{"gamepad", 1.5f}}}}},
			{{"deadZone", 100.0f}, {"mouseAreaSizeX", -1.0f}},
			{{"deadZone", 100.0f}, {"mouseAreaPosX", 1.0e100}},
			{{"deadZone", 100.0f}, {"mouseReleaseModKey", 256}},
			{{"deadZone", std::numeric_limits<float>::quiet_NaN()}},
		}};
		for (const auto& document : invalid) {
			if (InputDeviceConfigurationSerialization::TryRead(document, restored) ||
				InputDeviceConfigurationSerialization::Write(restored) != saved) {
				return false;
			}
		}

		// 閾値の補正と省略された項目の既存動作を維持
		if (!InputDeviceConfigurationSerialization::TryRead({{"deadZone", 50000.0f}}, restored) ||
			restored.deadZone != InputDeviceConfiguration::kMaxStickValue || restored.backgroundInputEnabled ||
			restored.playerGamepads[0] != -1 || restored.mouseAreaSize != configuration.mouseAreaSize) {
			return false;
		}
		if (!InputDeviceConfigurationSerialization::TryRead({{"deadZone", -1.0f}}, restored) || restored.deadZone != 0.0f) {
			return false;
		}

		// 初回更新前も入力は未押下で初期化される
		const InputDeviceState state;
		if (state.leftThumbX != 0.0f || state.leftThumbY != 0.0f || state.rightThumbX != 0.0f || state.rightThumbY != 0.0f ||
			state.wheelValue != 0.0f || state.mouseButtons != std::array<bool, 3>{} ||
			state.mousePreButtons != std::array<bool, 3>{}) {

			return false;
		}
		// 不正な振動値は予約せず次の有効な要求を受け付ける
		InputVibrationPlayer vibration;
		InputVibrationParams valid{};
		valid.left = 0.5f;
		valid.right = 0.5f;
		valid.duration = 1.0f;
		for (const float value : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
				 -std::numeric_limits<float>::infinity()}) {

			for (float InputVibrationParams::* field : {&InputVibrationParams::left, &InputVibrationParams::right,
					 &InputVibrationParams::duration, &InputVibrationParams::attack, &InputVibrationParams::release}) {

				InputVibrationParams invalidVibration = valid;
				invalidVibration.*field = value;
				if (vibration.PlayVibration(invalidVibration) != 0) {
					return false;
				}
			}
		}
		return vibration.PlayVibration(valid) == 1;
	}

}
