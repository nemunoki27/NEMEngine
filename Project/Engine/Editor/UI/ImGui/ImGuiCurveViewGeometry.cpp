#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace Engine::CurveEditorUtility {

	// 表示開始時刻を負にしない
	float ClampVisibleTimeMin(float v) {
		return (std::max)(0.0f, v);
	}

	// 時間軸の表示倍率を制限する
	float SafePixelsPerSecond(float v) {
		return (std::clamp)(v, 32.0f, 512.0f);
	}

	// 値軸の表示倍率を制限する
	float SafePixelsPerValue(float v) {
		return (std::clamp)(v, 24.0f, 320.0f);
	}

	// 読みやすい目盛り間隔を選ぶ
	float NiceStep(float rawStep) {

		if (rawStep <= 0.0f) {
			return 0.1f;
		}

		const float exponent = std::floor(std::log10(rawStep));
		const float base = std::pow(10.0f, exponent);
		const float fraction = rawStep / base;

		float niceFraction = 1.0f;
		if (fraction <= 1.0f) {
			niceFraction = 1.0f;
		} else if (fraction <= 2.0f) {
			niceFraction = 2.0f;
		} else if (fraction <= 5.0f) {
			niceFraction = 5.0f;
		} else {
			niceFraction = 10.0f;
		}

		return niceFraction * base;
	}

	// ゼロ幅の表示範囲を除算から保護する
	float SafeRange(float minValue, float maxValue) {

		const float range = maxValue - minValue;
		return std::abs(range) <= 0.00001f ? 1.0f : range;
	}

	// 時間軸の表示範囲を更新する
	void UpdateHorizontalViewRange(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.pixelsPerSecond = SafePixelsPerSecond(state.pixelsPerSecond);

		const float graphW = (std::max)(1.0f, graphRect.GetWidth());
		const float timeRange = graphW / state.pixelsPerSecond;

		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = (std::max)(state.visibleTimeMin + timeRange, state.visibleTimeMin + 0.001f);

		const float desiredTimeStep = 80.0f / state.pixelsPerSecond; // 約80pxごと
		state.gridTimeStep = NiceStep(desiredTimeStep);
	}

	// 固定時間範囲を表示へ反映する
	void ApplyFixedTimeRange(
		const ImRect& graphRect, const Engine::CurveEditSetting& setting, Engine::CurveEditorState& state) {

		if (!setting.fixedTimeRange) {
			return;
		}
		state.visibleTimeMin = setting.fixedTimeMin;
		state.visibleTimeMax = (std::max)(setting.fixedTimeMax, setting.fixedTimeMin + 0.001f);
		state.pixelsPerSecond =
			SafePixelsPerSecond((std::max)(1.0f, graphRect.GetWidth()) / SafeRange(state.visibleTimeMin, state.visibleTimeMax));
		state.gridTimeStep = (std::max)(0.0001f, setting.fixedTimeStep);
	}

	// 値範囲に表示倍率を合わせる
	void UpdateVerticalZoomFromRange(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.visibleValueMax = (std::max)(state.visibleValueMax, state.visibleValueMin + 0.001f);

		const float graphH = (std::max)(1.0f, graphRect.GetHeight());
		const float valueRange = SafeRange(state.visibleValueMin, state.visibleValueMax);

		state.pixelsPerValue = SafePixelsPerValue(graphH / valueRange);

		const float desiredValueStep = 40.0f / state.pixelsPerValue;
		state.gridValueStep = NiceStep(desiredValueStep);
	}

	// キー値を表示範囲へ収める
	float ClampKeyValueToVisibleRange(const Engine::CurveEditorState& state, float value) {

		return (std::clamp)(value, state.visibleValueMin, state.visibleValueMax);
	}

	// 現在の範囲に目盛り間隔を合わせる
	void RefreshGridStepsOnly(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = (std::max)(state.visibleTimeMax, state.visibleTimeMin + 0.001f);
		state.visibleValueMax = (std::max)(state.visibleValueMax, state.visibleValueMin + 0.001f);

		const float graphW = (std::max)(1.0f, graphRect.GetWidth());
		const float graphH = (std::max)(1.0f, graphRect.GetHeight());

		// 画面上でだいたいこのくらいの間隔で線を出す
		const float targetPixelX = 80.0f;
		const float targetPixelY = 40.0f;

		const float timeRange = SafeRange(state.visibleTimeMin, state.visibleTimeMax);
		const float valueRange = SafeRange(state.visibleValueMin, state.visibleValueMax);

		const float desiredTimeStep = timeRange * (targetPixelX / graphW);
		const float desiredValueStep = valueRange * (targetPixelY / graphH);

		state.gridTimeStep = NiceStep(desiredTimeStep);
		state.gridValueStep = NiceStep(desiredValueStep);
	}

	// マウス位置を基準に時間軸を拡縮する
	void ZoomTimeAroundMouse(
		const ImRect& graphRect, Engine::CurveEditorState& state, const ImVec2& mousePos, float wheelDelta) {

		const float oldPixelsPerSecond = state.pixelsPerSecond;
		const float newPixelsPerSecond = SafePixelsPerSecond(oldPixelsPerSecond * (wheelDelta > 0.0f ? 1.15f : (1.0f / 1.15f)));

		const float mouseRatioX = (mousePos.x - graphRect.Min.x) / (std::max)(1.0f, graphRect.GetWidth());
		const float worldTime = state.visibleTimeMin + (state.visibleTimeMax - state.visibleTimeMin) * mouseRatioX;

		state.pixelsPerSecond = newPixelsPerSecond;
		const float newRange = graphRect.GetWidth() / state.pixelsPerSecond;
		state.visibleTimeMin = worldTime - newRange * mouseRatioX;
		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = state.visibleTimeMin + newRange;
	}

	// 有効な間隔へキー時刻を丸める
	float SnapTime(float time, bool enableSnap, float interval) {

		if (!enableSnap || interval <= 0.0f) {
			return time;
		}
		return std::round(time / interval) * interval;
	}

	// キー時刻を許可範囲へ収める
	float ClampKeyTime(const Engine::CurveEditorState& state, float time) {

		time = (std::max)(0.0f, time);
		if (0.0f < state.maxKeyTime) {
			time = (std::min)(time, state.maxKeyTime);
		}
		return time;
	}

	// ゼロ近傍の表示を揃える
	float NormalizeGridValue(float value) {

		return std::abs(value) <= 0.00001f ? 0.0f : value;
	}

	// 目盛り間隔に表示桁数を合わせる
	std::string FormatGridValue(float value, float step) {

		value = NormalizeGridValue(value);
		if (1.0f <= step) {
			return std::format("{:.0f}", value);
		}
		if (0.1f <= step) {
			return std::format("{:.2f}", value);
		}
		return std::format("{:.3f}", value);
	}

	// カーブ座標を画面座標へ変換する
	ImVec2 WorldToScreen(const ImRect& rect, const Engine::CurveEditorState& state, float time, float value) {

		const float tx = (time - state.visibleTimeMin) / SafeRange(state.visibleTimeMin, state.visibleTimeMax);
		const float ty = (value - state.visibleValueMin) / SafeRange(state.visibleValueMin, state.visibleValueMax);
		return ImVec2(rect.Min.x + tx * rect.GetWidth(), rect.Max.y - ty * rect.GetHeight());
	}

	// 画面座標をカーブ座標へ変換する
	ImVec2 ScreenToWorld(const ImRect& rect, const Engine::CurveEditorState& state, const ImVec2& pos) {

		const float tx = (pos.x - rect.Min.x) / (std::max)(1.0f, rect.GetWidth());
		const float ty = (rect.Max.y - pos.y) / (std::max)(1.0f, rect.GetHeight());
		return ImVec2(state.visibleTimeMin + tx * (state.visibleTimeMax - state.visibleTimeMin),
			state.visibleValueMin + ty * (state.visibleValueMax - state.visibleValueMin));
	}

	// 矩形内のマウス位置を判定する
	bool RectContains(const ImRect& rect, const ImVec2& pos) {

		return rect.Min.x <= pos.x && pos.x <= rect.Max.x && rect.Min.y <= pos.y && pos.y <= rect.Max.y;
	}

	// 矩形の始点と終点を揃える
	void NormalizeRect(ImVec2& minPos, ImVec2& maxPos) {

		if (maxPos.x < minPos.x) {
			std::swap(minPos.x, maxPos.x);
		}
		if (maxPos.y < minPos.y) {
			std::swap(minPos.y, maxPos.y);
		}
	}

	// キー全体が収まる表示範囲にする
	void FitView(const ImRect& graphRect, std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state) {

		bool hasRange = false;
		float timeMin = 0.0f;
		float timeMax = 1.0f;
		float valueMin = -1.0f;
		float valueMax = 1.0f;

		for (uint32_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
			if (!state.IsChannelVisible(channelIndex)) {
				continue;
			}

			float channelTimeMin = 0.0f;
			float channelTimeMax = 0.0f;
			float channelValueMin = 0.0f;
			float channelValueMax = 0.0f;
			if (!channels[channelIndex].GetTimeRange(channelTimeMin, channelTimeMax) ||
				!channels[channelIndex].GetValueRange(channelValueMin, channelValueMax)) {
				continue;
			}

			if (!hasRange) {
				timeMin = channelTimeMin;
				timeMax = channelTimeMax;
				valueMin = channelValueMin;
				valueMax = channelValueMax;
				hasRange = true;
			} else {
				timeMin = (std::min)(timeMin, channelTimeMin);
				timeMax = (std::max)(timeMax, channelTimeMax);
				valueMin = (std::min)(valueMin, channelValueMin);
				valueMax = (std::max)(valueMax, channelValueMax);
			}
		}

		if (!hasRange) {
			state.visibleTimeMin = 0.0f;
			state.visibleTimeMax = 1.0f;
			state.visibleValueMin = 0.0f;
			state.visibleValueMax = 1.0f;
			state.pixelsPerSecond = 120.0f;
			state.pixelsPerValue = 80.0f;
			state.gridTimeStep = NiceStep(80.0f / state.pixelsPerSecond);
			state.gridValueStep = NiceStep(40.0f / state.pixelsPerValue);
			return;
		}

		const float timePadding = (std::max)(0.1f, (timeMax - timeMin) * 0.08f);

		state.visibleTimeMin = (std::max)(0.0f, timeMin - timePadding);
		state.visibleTimeMax = (std::max)(state.visibleTimeMin + 0.001f, timeMax + timePadding);

		// Frameでは値方向の表示範囲は変更しない
		// MinValue / MaxValueはユーザーが決めた値をそのまま維持する
		// 実際のグラフサイズからズーム係数を同期する
		const float graphW = (std::max)(1.0f, graphRect.GetWidth());

		state.pixelsPerSecond = SafePixelsPerSecond(graphW / SafeRange(state.visibleTimeMin, state.visibleTimeMax));
		state.gridTimeStep = NiceStep(80.0f / state.pixelsPerSecond);

		// Frame直後の見た目と、その後の通常操作の基準を揃える
		UpdateVerticalZoomFromRange(graphRect, state);
	}
} // Engine::CurveEditorUtility
