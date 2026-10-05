#include "CurveEditorContractTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiCurveEditorInternal.h>

// c++
#include <array>
#include <cmath>

namespace {

	using namespace Engine;
	using namespace Engine::CurveEditorUtility;

	// 固定範囲と座標変換を同じ基準で確認する
	bool TestCurveViewCoordinates() {

		CurveEditorState state;
		CurveEditSetting setting;
		setting.fixedTimeRange = true;
		setting.fixedTimeMin = 0.0f;
		setting.fixedTimeMax = 1.0f;
		setting.fixedTimeStep = 0.1f;
		state.visibleValueMin = -2.0f;
		state.visibleValueMax = 3.0f;
		const ImRect graph(ImVec2(10.0f, 20.0f), ImVec2(310.0f, 220.0f));
		ApplyFixedTimeRange(graph, setting, state);
		const auto screen = WorldToScreen(graph, state, 0.25f, 1.0f);
		const auto restored = ScreenToWorld(graph, state, screen);
		if (std::abs(screen.x - 85.0f) > 0.001f || std::abs(screen.y - 100.0f) > 0.001f ||
			std::abs(restored.x - 0.25f) > 0.001f || std::abs(restored.y - 1.0f) > 0.001f) {
			return false;
		}
		state.maxKeyTime = 1.0f;
		return state.gridTimeStep == 0.1f && ClampKeyTime(state, -1.0f) == 0.0f && ClampKeyTime(state, 3.0f) == 1.0f &&
			   SnapTime(0.34f, false, 0.1f) == 0.34f && std::abs(SnapTime(0.34f, true, 0.1f) - 0.3f) < 0.001f;
	}

	// RGB代表キーの重複選択を一度だけ削除する
	bool TestCurveColorSelection() {

		CurveColor4 curve;
		const auto channels = GetCurveChannels(curve);
		AddColorRGBKey(channels, 0.2f);
		AddColorRGBKey(channels, 0.8f);
		channels[3].AddKey(0.2f, 0.5f);
		CurveEditorState state;
		state.selectedKeys = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
		DeleteSelectedKeys(channels, state, nullptr);
		if (!state.selectedKeys.empty() || !channels[3].keys.empty()) {
			return false;
		}
		for (size_t index = 0; index < 3; ++index) {
			if (channels[index].keys.size() != 1 || channels[index].keys.front().time != 0.8f) {
				return false;
			}
		}
		return true;
	}

	// 並替えと削除で軸の補助情報を同じキーへ保つ
	bool TestCurveQuaternionKeyOwnership() {

		CurveQuaternion curve;
		curve.channels[0].keys = {{0.8f, 1.0f}, {0.2f, 2.0f}};
		curve.channels[1].keys = {{0.8f, 90.0f}, {0.2f, 30.0f}};
		CurveQuaternionAxisKey late, early;
		late.axes = {Axis::Y};
		early.axes = {Axis::Z};
		curve.axisKeys = {late, early};
		const auto channels = GetCurveChannels(curve);
		SortQuaternionKeys(channels, &curve.axisKeys);
		if (curve.axisKeys.front().axes.front() != Axis::Z || curve.channels[0].keys.front().time != 0.2f ||
			curve.channels[0].keys.front().interpolation != CurveInterpolationMode::Constant) {
			return false;
		}
		CurveEditorState state;
		state.selectedKeys = {{0, 0}};
		DeleteSelectedKeys(channels, state, &curve.axisKeys);
		if (curve.axisKeys.size() != 1 || curve.axisKeys.front().axes.front() != Axis::Y || channels[0].keys.size() != 1 ||
			channels[1].keys.size() != 2) {
			return false;
		}
		state.selectedKeys = {{1, 0}};
		DeleteSelectedChannelKeys(channels, state, &curve.axisKeys);
		return channels[1].keys.empty() && channels[0].keys.size() == 1 && curve.axisKeys.size() == 1;
	}
}

bool NEMTests::TestCurveEditorContracts() {

	return TestCurveViewCoordinates() && TestCurveColorSelection() && TestCurveQuaternionKeyOwnership();
}
