#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace {

	// 非有限値を除いてfloatの範囲へ収める
	float FiniteFloat(double value, float fallback = 0.0f) {

		if (!std::isfinite(value)) {
			return std::isfinite(fallback) ? fallback : 0.0f;
		}
		return static_cast<float>((std::clamp)(value,
			static_cast<double>(std::numeric_limits<float>::lowest()),
			static_cast<double>(std::numeric_limits<float>::max())));
	}
}

namespace Engine::CurveEditorUtility {

	// 表示開始時刻を負にしない
	float ClampVisibleTimeMin(float v) {

		return (std::max)(0.0f, FiniteFloat(v));
	}

	// 時間軸の表示倍率を制限する
	float SafePixelsPerSecond(double v) {

		return std::isfinite(v) ? static_cast<float>((std::clamp)(v, 32.0, 512.0)) : 120.0f;
	}

	// 値軸の表示倍率を制限する
	float SafePixelsPerValue(double v) {

		return std::isfinite(v) ? static_cast<float>((std::clamp)(v, 24.0, 320.0)) : 80.0f;
	}

	// 読みやすい目盛り間隔を選ぶ
	float NiceStep(float rawStep) {

		if (!std::isfinite(rawStep) || rawStep <= 0.0f) {
			return 0.1f;
		}

		const float exponent = std::floor(std::log10(rawStep));
		const double base = std::pow(10.0, static_cast<double>(exponent));
		const double fraction = rawStep / base;

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

		return static_cast<float>((std::min)(niceFraction * base, static_cast<double>(std::numeric_limits<float>::max())));
	}

	CurveGridRange BuildGridRange(float minValue, float maxValue, float step) {

		// 無効な範囲から目盛りを作らない
		if (!std::isfinite(minValue) || !std::isfinite(maxValue) || !std::isfinite(step) ||
			maxValue < minValue || step <= 0.0f) {
			return {};
		}
		// 広い範囲では目盛り間隔を広げる
		constexpr uint32_t kMaxGridLines = 4096;
		const double range = static_cast<double>(maxValue) - minValue;
		const double interval = (std::max)(static_cast<double>(step), range / (kMaxGridLines - 1));
		const double begin = std::ceil(static_cast<double>(minValue) / interval - 0.000001) * interval;
		const double end = (std::min)(static_cast<double>(maxValue) + interval * 0.5,
			static_cast<double>(std::numeric_limits<float>::max()));
		const double count = (std::max)(0.0, std::floor((end - begin) / interval) + 1.0);
		return {begin, interval, static_cast<uint32_t>((std::min)(count, static_cast<double>(kMaxGridLines)))};
	}

	// ゼロ幅の表示範囲を除算から保護する
	double SafeRange(float minValue, float maxValue) {

		// float両端の差もdoubleで保持する
		const double range = static_cast<double>(maxValue) - minValue;
		return !std::isfinite(range) || std::abs(range) <= 0.00001 ? 1.0 : range;
	}

	// 時間軸の表示範囲を更新する
	void UpdateHorizontalViewRange(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.pixelsPerSecond = SafePixelsPerSecond(state.pixelsPerSecond);

		const float graphW = (std::max)(1.0f, graphRect.GetWidth());
		const float timeRange = graphW / state.pixelsPerSecond;

		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = FiniteFloat(static_cast<double>(state.visibleTimeMin) + (std::max)(timeRange, 0.001f));

		const float desiredTimeStep = 80.0f / state.pixelsPerSecond; // 約80pxごと
		state.gridTimeStep = NiceStep(desiredTimeStep);
	}

	// 固定時間範囲を表示へ反映する
	void ApplyFixedTimeRange(
		const ImRect& graphRect, const Engine::CurveEditSetting& setting, Engine::CurveEditorState& state) {

		if (!setting.fixedTimeRange) {
			return;
		}
		state.visibleTimeMin = FiniteFloat(setting.fixedTimeMin);
		state.visibleTimeMax = (std::max)(FiniteFloat(setting.fixedTimeMax, state.visibleTimeMin),
			FiniteFloat(static_cast<double>(state.visibleTimeMin) + 0.001));
		state.pixelsPerSecond =
			SafePixelsPerSecond((std::max)(1.0f, graphRect.GetWidth()) / SafeRange(state.visibleTimeMin, state.visibleTimeMax));
		state.gridTimeStep = (std::max)(0.0001f, FiniteFloat(setting.fixedTimeStep, 0.1f));
	}

	// 値範囲に表示倍率を合わせる
	void UpdateVerticalZoomFromRange(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.visibleValueMin = FiniteFloat(state.visibleValueMin);
		state.visibleValueMax = (std::max)(FiniteFloat(state.visibleValueMax, state.visibleValueMin),
			FiniteFloat(static_cast<double>(state.visibleValueMin) + 0.001));

		const float graphH = (std::max)(1.0f, graphRect.GetHeight());
		const double valueRange = SafeRange(state.visibleValueMin, state.visibleValueMax);

		state.pixelsPerValue = SafePixelsPerValue(graphH / valueRange);

		const float desiredValueStep = 40.0f / state.pixelsPerValue;
		state.gridValueStep = NiceStep(desiredValueStep);
	}

	// キー値を表示範囲へ収める
	float ClampKeyValueToVisibleRange(const Engine::CurveEditorState& state, float value) {

		// 逆転した表示範囲も順序を揃えて扱う
		const float first = FiniteFloat(state.visibleValueMin);
		const float second = FiniteFloat(state.visibleValueMax, first);
		return (std::clamp)(FiniteFloat(value, first), (std::min)(first, second), (std::max)(first, second));
	}

	// 現在の範囲に目盛り間隔を合わせる
	void RefreshGridStepsOnly(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = (std::max)(FiniteFloat(state.visibleTimeMax, state.visibleTimeMin),
			FiniteFloat(static_cast<double>(state.visibleTimeMin) + 0.001));
		state.visibleValueMin = FiniteFloat(state.visibleValueMin);
		state.visibleValueMax = (std::max)(FiniteFloat(state.visibleValueMax, state.visibleValueMin),
			FiniteFloat(static_cast<double>(state.visibleValueMin) + 0.001));

		const float graphW = (std::max)(1.0f, graphRect.GetWidth());
		const float graphH = (std::max)(1.0f, graphRect.GetHeight());

		// 読みやすい間隔で目盛りを出す
		const float targetPixelX = 80.0f;
		const float targetPixelY = 40.0f;

		const double timeRange = SafeRange(state.visibleTimeMin, state.visibleTimeMax);
		const double valueRange = SafeRange(state.visibleValueMin, state.visibleValueMax);

		const float desiredTimeStep = FiniteFloat(timeRange * (targetPixelX / graphW));
		const float desiredValueStep = FiniteFloat(valueRange * (targetPixelY / graphH));

		state.gridTimeStep = NiceStep(desiredTimeStep);
		state.gridValueStep = NiceStep(desiredValueStep);
	}

	// マウス位置を基準に時間軸を拡縮する
	void ZoomTimeAroundMouse(
		const ImRect& graphRect, Engine::CurveEditorState& state, const ImVec2& mousePos, float wheelDelta) {

		const float oldPixelsPerSecond = state.pixelsPerSecond;
		const float newPixelsPerSecond =
			SafePixelsPerSecond(static_cast<double>(oldPixelsPerSecond) * (wheelDelta > 0.0f ? 1.15 : (1.0 / 1.15)));

		const double width = (std::max)(1.0, static_cast<double>(graphRect.Max.x) - graphRect.Min.x);
		const double mouseRatioX = (static_cast<double>(mousePos.x) - graphRect.Min.x) / width;
		const double worldTime = state.visibleTimeMin +
			(static_cast<double>(state.visibleTimeMax) - state.visibleTimeMin) * mouseRatioX;

		state.pixelsPerSecond = newPixelsPerSecond;
		const double newRange = width / state.pixelsPerSecond;
		state.visibleTimeMin = FiniteFloat(worldTime - newRange * mouseRatioX, state.visibleTimeMin);
		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = FiniteFloat(state.visibleTimeMin + newRange);
	}

	// 有効な間隔へキー時刻を丸める
	float SnapTime(float time, bool enableSnap, float interval) {

		time = FiniteFloat(time);
		if (!enableSnap || !std::isfinite(interval) || interval <= 0.0f) {
			return time;
		}
		// 小さい間隔でも途中の除算を溢れさせない
		return FiniteFloat(std::round(static_cast<double>(time) / interval) * interval, time);
	}

	// キー時刻を許可範囲へ収める
	float ClampKeyTime(const Engine::CurveEditorState& state, float time) {

		time = (std::max)(0.0f, FiniteFloat(time));
		if (0.0f < state.maxKeyTime) {
			time = (std::min)(time, state.maxKeyTime);
		}
		return time;
	}

	// ゼロ近傍の表示を揃える
	float NormalizeGridValue(float value) {

		value = FiniteFloat(value);
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

	bool IsMajorGridLine(float value, float step) {

		// 無効な間隔と除算結果を描画判定へ渡さない
		if (!std::isfinite(value) || !std::isfinite(step) || step <= 0.0f) {
			return false;
		}
		const float index = std::round(value / step);
		return std::isfinite(index) && std::fmod(index, static_cast<float>(kCurveGridMajorInterval)) == 0.0f;
	}

	// カーブ座標を画面座標へ変換する
	ImVec2 WorldToScreen(const ImRect& rect, const Engine::CurveEditorState& state, float time, float value) {

		// 範囲差と位置の補間はdoubleで計算する
		const double tx = (static_cast<double>(time) - state.visibleTimeMin) /
			SafeRange(state.visibleTimeMin, state.visibleTimeMax);
		const double ty = (static_cast<double>(value) - state.visibleValueMin) /
			SafeRange(state.visibleValueMin, state.visibleValueMax);
		return ImVec2(FiniteFloat(rect.Min.x + tx * (static_cast<double>(rect.Max.x) - rect.Min.x), rect.Min.x),
			FiniteFloat(rect.Max.y - ty * (static_cast<double>(rect.Max.y) - rect.Min.y), rect.Max.y));
	}

	// 画面座標をカーブ座標へ変換する
	ImVec2 ScreenToWorld(const ImRect& rect, const Engine::CurveEditorState& state, const ImVec2& pos) {

		const double tx = (static_cast<double>(pos.x) - rect.Min.x) /
			(std::max)(1.0, static_cast<double>(rect.Max.x) - rect.Min.x);
		const double ty = (static_cast<double>(rect.Max.y) - pos.y) /
			(std::max)(1.0, static_cast<double>(rect.Max.y) - rect.Min.y);
		return ImVec2(FiniteFloat(state.visibleTimeMin + tx *
			(static_cast<double>(state.visibleTimeMax) - state.visibleTimeMin), state.visibleTimeMin),
			FiniteFloat(state.visibleValueMin + ty *
			(static_cast<double>(state.visibleValueMax) - state.visibleValueMin), state.visibleValueMin));
	}

	// 境界を含めて矩形内の位置を判定する
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

		const double timePadding = (std::max)(0.1, (static_cast<double>(timeMax) - timeMin) * 0.08);

		state.visibleTimeMin = (std::max)(0.0f, FiniteFloat(timeMin - timePadding));
		state.visibleTimeMax = (std::max)(FiniteFloat(static_cast<double>(state.visibleTimeMin) + 0.001),
			FiniteFloat(timeMax + timePadding));

		// キーがある場合は指定した値範囲で倍率を揃える
		const float graphW = (std::max)(1.0f, graphRect.GetWidth());

		state.pixelsPerSecond = SafePixelsPerSecond(graphW / SafeRange(state.visibleTimeMin, state.visibleTimeMax));
		state.gridTimeStep = NiceStep(80.0f / state.pixelsPerSecond);

		// 値範囲の最小幅と表示倍率を揃える
		UpdateVerticalZoomFromRange(graphRect, state);
	}
} // Engine::CurveEditorUtility
