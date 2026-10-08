#include "CurveEditorContractTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiCurveEditorInternal.h>
#include <Engine/Core/Animation/Clips/AnimationChannelUtility.h>
#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// c++
#include <array>
#include <cmath>
#include <limits>

namespace {

	using namespace Engine;
	using namespace Engine::CurveEditorUtility;

	// 極端な範囲でも目盛りの列挙を有限回で終える
	bool TestCurveGridRange() {

		const auto normal = BuildGridRange(0.0f, 1.0f, 0.1f);
		const auto negative = BuildGridRange(-1.0f, 0.0f, 0.1f);
		const auto narrow = BuildGridRange(100000000.0f, 100000008.0f, 0.0001f);
		const float limit = std::numeric_limits<float>::max();
		const auto wide = BuildGridRange(-limit, limit, 0.0001f);
		return normal.count == 11 && negative.count == 11 && normal.begin == 0.0 &&
			narrow.count > 0 && narrow.count <= 4096 && wide.count > 0 && wide.count <= 4096 &&
			std::isfinite(wide.begin) && std::isfinite(wide.step) &&
			BuildGridRange(1.0f, 0.0f, 0.1f).count == 0 && BuildGridRange(0.0f, 1.0f, 0.0f).count == 0 &&
			BuildGridRange(0.0f, std::numeric_limits<float>::infinity(), 0.1f).count == 0 &&
			BuildGridRange(0.0f, 1.0f, std::numeric_limits<float>::quiet_NaN()).count == 0;
	}

	// 不揃いのチャンネルと無効な軸値を処理する
	bool TestCurveSharedCountsAndAxes() {

		CurveColor4 curve;
		curve.channels[0].AddKey(0.0f, 1.0f);
		curve.channels[0].AddKey(1.0f, 0.0f);
		curve.channels[1].AddKey(0.0f, 0.5f);
		curve.channels[2].AddKey(0.0f, 0.5f);
		auto channels = GetCurveChannels(curve);
		if (AnimationChannelUtility::GetSharedKeyCount({}) != 0 ||
			AnimationChannelUtility::GetSharedKeyCount(channels.first(3)) != 1 ||
			AnimationChannelUtility::GetSharedKeyCount(channels) != 0) {
			return false;
		}
		CurveQuaternion rotation;
		rotation.channels[0].keys = {{0.0f, std::numeric_limits<float>::quiet_NaN()}};
		auto rotationChannels = GetCurveChannels(rotation);
		if (GetQuaternionAxisKey(rotationChannels, {}, 0).axes.front() != Axis::X) {
			return false;
		}
		rotation.channels[0].keys[0].value = std::numeric_limits<float>::max();
		if (GetQuaternionAxisKey(rotationChannels, {}, 0).axes.front() != Axis::Z) {
			return false;
		}
		CurveQuaternionAxisKey axis;
		axis.useCustomAxis = true;
		axis.customAxis = {std::numeric_limits<float>::infinity(), 0.0f, 0.0f};
		const Vector3 direction = QuaternionAxisKeyUtility::GetAxisDirection(axis);
		const Vector3 sanitized = QuaternionAxisKeyUtility::Sanitize(axis).customAxis;
		return direction.x == 1.0f && direction.y == 0.0f && direction.z == 0.0f &&
			sanitized.x == 1.0f && sanitized.y == 0.0f && sanitized.z == 0.0f &&
			IsMajorGridLine(0.8f, 0.1f) && IsMajorGridLine(-0.8f, 0.1f) && !IsMajorGridLine(0.7f, 0.1f) &&
			!IsMajorGridLine(1.0f, 0.0f) && !IsMajorGridLine(std::numeric_limits<float>::max(), 0.0001f);
	}

	// 表示マスクと選択の範囲外アクセスを防ぐ
	bool TestCurveChannelBounds() {

		CurveEditorState state;
		state.SetChannelVisible(63, false);
		if (state.IsChannelVisible(63) || state.channelVisible.size() != 64) {
			return false;
		}
		const uint64_t mask = state.activeChannelMask;
		state.SetChannelVisible(64, true);
		state.SetChannelVisible(UINT32_MAX, false);
		if (state.activeChannelMask != mask || state.channelVisible.size() != 64 || state.IsChannelVisible(64)) {
			return false;
		}
		std::array<uint32_t, 65> keyCounts{};
		keyCounts[64] = 1;
		state.selectedKeys = {{64, 0}, {64, 1}, {65, 0}, {UINT32_MAX, 0}};
		state.RemoveInvalidSelections(keyCounts);
		if (state.selectedKeys.size() != 1 || !state.IsSelected(64, 0)) {
			return false;
		}
		state.RemoveInvalidSelections({});
		return state.selectedKeys.empty();
	}

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

	// 極端な範囲でも座標とキー値を有限に保つ
	bool TestCurveViewNumericLimits() {

		const float limit = std::numeric_limits<float>::max();
		const float invalid = std::numeric_limits<float>::quiet_NaN();
		const ImRect graph(ImVec2(10.0f, 20.0f), ImVec2(310.0f, 220.0f));
		CurveEditorState state;
		state.visibleTimeMin = state.visibleValueMin = -limit;
		state.visibleTimeMax = state.visibleValueMax = limit;
		const ImVec2 screen = WorldToScreen(graph, state, 0.0f, 0.0f);
		const ImVec2 restored = ScreenToWorld(graph, state, ImVec2(160.0f, 120.0f));
		if (screen.x != 160.0f || screen.y != 120.0f || restored.x != 0.0f || restored.y != 0.0f) {
			return false;
		}

		// float最大値の外挿も描画へ非有限値を渡さない
		state.visibleTimeMin = state.visibleValueMin = 0.0f;
		state.visibleTimeMax = state.visibleValueMax = 0.1f;
		const ImVec2 outside = WorldToScreen(graph, state, limit, -limit);
		if (!std::isfinite(outside.x) || !std::isfinite(outside.y)) {
			return false;
		}

		state.visibleValueMin = 4.0f;
		state.visibleValueMax = -2.0f;
		if (ClampKeyValueToVisibleRange(state, 3.0f) != 3.0f ||
			!std::isfinite(ClampKeyValueToVisibleRange(state, invalid))) {
			return false;
		}
		state.pixelsPerSecond = invalid;
		UpdateHorizontalViewRange(graph, state);
		state.visibleValueMin = -limit;
		state.visibleValueMax = limit;
		UpdateVerticalZoomFromRange(graph, state);
		if (state.pixelsPerSecond != 120.0f || !std::isfinite(state.pixelsPerValue)) {
			return false;
		}
		CurveEditSetting setting;
		setting.fixedTimeRange = true;
		setting.fixedTimeMin = setting.fixedTimeMax = setting.fixedTimeStep = invalid;
		ApplyFixedTimeRange(graph, setting, state);
		return std::isfinite(state.visibleTimeMin) && std::isfinite(state.visibleTimeMax) &&
			std::isfinite(state.gridTimeStep) &&
			SnapTime(limit, true, std::numeric_limits<float>::denorm_min()) == limit &&
			SnapTime(0.34f, true, invalid) == 0.34f && ClampKeyTime(state, invalid) == 0.0f;
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

	return TestCurveGridRange() && TestCurveSharedCountsAndAxes() && TestCurveChannelBounds() && TestCurveViewCoordinates() &&
		TestCurveViewNumericLimits() && TestCurveColorSelection() && TestCurveQuaternionKeyOwnership();
}
