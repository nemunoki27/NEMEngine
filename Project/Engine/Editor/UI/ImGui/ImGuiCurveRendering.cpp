#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationChannelUtility.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::CurveEditorUtility {

	// 色をImGui描画色へ変換する
	ImU32 ToImU32(const Engine::Color4& color) {

		return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
	}

	// 表示範囲の目盛りを描く
	void DrawGrid(const ImRect& rect, const Engine::CurveEditorState& state) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const ImU32 backgroundColor = IM_COL32(5, 5, 5, 255);
		const ImU32 minorColor = IM_COL32(38, 38, 42, 180);

		drawList->AddRectFilled(rect.Min, rect.Max, backgroundColor);

		const float timeStep = (std::max)(0.0001f, state.gridTimeStep);
		const float valueStep = (std::max)(0.0001f, state.gridValueStep);

		// 時刻0より前には目盛りを出さない
		const CurveGridRange timeRange = BuildGridRange((std::max)(0.0f, state.visibleTimeMin), state.visibleTimeMax, timeStep);
		for (uint32_t index = 0; index < timeRange.count; ++index) {
			const float time = static_cast<float>(timeRange.begin + timeRange.step * index);
			const ImVec2 pos = WorldToScreen(rect, state, time, state.visibleValueMin);
			drawList->AddLine(ImVec2(pos.x, rect.Min.y), ImVec2(pos.x, rect.Max.y), minorColor);
		}

		const CurveGridRange valueRange = BuildGridRange(state.visibleValueMin, state.visibleValueMax, valueStep);
		for (uint32_t index = 0; index < valueRange.count; ++index) {
			const float value = static_cast<float>(valueRange.begin + valueRange.step * index);
			const ImVec2 pos = WorldToScreen(rect, state, state.visibleTimeMin, value);
			drawList->AddLine(ImVec2(rect.Min.x, pos.y), ImVec2(rect.Max.x, pos.y), minorColor);
		}

		// 時刻0の軸線を描く
		if (0.0f >= state.visibleTimeMin && 0.0f <= state.visibleTimeMax) {
			const ImVec2 p0 = WorldToScreen(rect, state, 0.0f, state.visibleValueMin);
			const ImVec2 p1 = WorldToScreen(rect, state, 0.0f, state.visibleValueMax);
			drawList->AddLine(ImVec2(p0.x, rect.Min.y), ImVec2(p1.x, rect.Max.y), kCurveAxisColor, 2.0f);
		}

		// 値0の軸線を描く
		if (0.0f >= state.visibleValueMin && 0.0f <= state.visibleValueMax) {
			const ImVec2 p0 = WorldToScreen(rect, state, state.visibleTimeMin, 0.0f);
			const ImVec2 p1 = WorldToScreen(rect, state, state.visibleTimeMax, 0.0f);
			drawList->AddLine(ImVec2(rect.Min.x, p0.y), ImVec2(rect.Max.x, p1.y), kCurveAxisColor, 2.0f);
		}
	}

	// 時間軸の目盛りを描く
	void DrawTimeRuler(const ImRect& rect, const Engine::CurveEditorState& state) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(8, 8, 8, 255));
		drawList->AddRect(rect.Min, rect.Max, IM_COL32(80, 80, 80, 255));

		const ImU32 textColor = IM_COL32(210, 210, 210, 255);
		const ImU32 tickColor = IM_COL32(110, 110, 116, 255);

		const float timeStep = (std::max)(0.0001f, state.gridTimeStep);
		const CurveGridRange timeRange = BuildGridRange((std::max)(0.0f, state.visibleTimeMin), state.visibleTimeMax, timeStep);
		for (uint32_t index = 0; index < timeRange.count; ++index) {
			const float time = static_cast<float>(timeRange.begin + timeRange.step * index);
			const bool major = IsMajorGridLine(time, timeStep);

			const ImVec2 pos = WorldToScreen(
				ImRect(ImVec2(rect.Min.x, rect.Min.y), ImVec2(rect.Max.x, rect.Max.y)), state, time, state.visibleValueMin);

			const float tickH = major ? 10.0f : 6.0f;
			drawList->AddLine(ImVec2(pos.x, rect.Max.y - tickH), ImVec2(pos.x, rect.Max.y), tickColor);

			// 目盛りがある位置にはすべて値を表示する
			const std::string text = FormatGridValue(time, timeStep);
			drawList->AddText(ImVec2(pos.x + 3.0f, rect.Min.y + 3.0f), textColor, text.c_str());
		}
	}

	// 値軸の目盛りを描く
	void DrawValueRuler(const ImRect& rect, const Engine::CurveEditorState& state) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(8, 8, 8, 255));
		drawList->AddRect(rect.Min, rect.Max, IM_COL32(80, 80, 80, 255));

		const ImU32 textColor = IM_COL32(210, 210, 210, 255);
		const ImU32 tickColor = IM_COL32(110, 110, 116, 255);

		const float valueStep = (std::max)(0.0001f, state.gridValueStep);
		const CurveGridRange valueRange = BuildGridRange(state.visibleValueMin, state.visibleValueMax, valueStep);
		for (uint32_t index = 0; index < valueRange.count; ++index) {
			const float value = static_cast<float>(valueRange.begin + valueRange.step * index);
			const bool major = IsMajorGridLine(value, valueStep);

			const ImVec2 pos = WorldToScreen(
				ImRect(ImVec2(rect.Min.x, rect.Min.y), ImVec2(rect.Max.x, rect.Max.y)), state, state.visibleTimeMin, value);

			const float tickW = major ? 10.0f : 6.0f;
			drawList->AddLine(ImVec2(rect.Max.x - tickW, pos.y), ImVec2(rect.Max.x, pos.y), tickColor);

			// 目盛りがある位置にはすべて値を表示する
			const std::string text = FormatGridValue(value, valueStep);
			const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());

			ImVec2 textPos(rect.Max.x - textSize.x - 4.0f, pos.y - textSize.y * 0.5f);

			// ルーラー矩形の中に収める
			textPos.x = (std::clamp)(textPos.x, rect.Min.x + 2.0f, (std::max)(rect.Min.x + 2.0f, rect.Max.x - textSize.x - 2.0f));
			textPos.y = (std::clamp)(textPos.y, rect.Min.y + 1.0f, (std::max)(rect.Min.y + 1.0f, rect.Max.y - textSize.y - 1.0f));

			drawList->AddText(textPos, textColor, text.c_str());
		}

		drawList->PopClipRect();
	}

	// 評価したカーブを線で描く
	void DrawCurveSamples(const ImRect& rect, const Engine::CurveChannel& channel, const Engine::CurveEditorState& state) {

		if (channel.keys.empty()) {
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		const ImU32 color = ToImU32(channel.displayColor);
		const int sampleCount = (std::max)(16, static_cast<int>(rect.GetWidth() / 4.0f));
		ImVec2 prev{};
		bool hasPrev = false;

		for (int i = 0; i <= sampleCount; ++i) {
			const float ratio = static_cast<float>(i) / static_cast<float>(sampleCount);
			const float time = state.visibleTimeMin + (state.visibleTimeMax - state.visibleTimeMin) * ratio;
			const float value = channel.Evaluate(time);
			const ImVec2 pos = WorldToScreen(rect, state, time, value);
			if (hasPrev) {
				drawList->AddLine(prev, pos, color, 2.0f);
			}
			prev = pos;
			hasPrev = true;
		}

		drawList->PopClipRect();
	}

	// RGBカーブを色付きで描く
	void DrawColorRGBCurveSamples(
		const ImRect& rect, std::span<Engine::CurveChannel> channels, const Engine::CurveEditorState& state) {

		if (!IsColorCurveSet(channels) || !state.IsChannelVisible(0)) {
			return;
		}

		const size_t rgbKeyCount = Engine::AnimationChannelUtility::GetSharedKeyCount(channels.first(3));
		if (rgbKeyCount == 0) {
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		ImVec2 prevPos{};
		bool hasPrev = false;
		for (uint32_t keyIndex = 0; keyIndex < rgbKeyCount; ++keyIndex) {
			const float time = channels[0].keys[keyIndex].time;
			const Engine::Color4 color = EvaluateCurveColorAtTime(channels, time);
			const float previewValue = (color.r + color.g + color.b) / 3.0f;
			const ImVec2 pos = WorldToScreen(rect, state, time, previewValue);
			const ImU32 lineColor = ToImU32(color);
			if (hasPrev) {
				drawList->AddLine(prevPos, pos, lineColor, 2.0f);
			}
			prevPos = pos;
			hasPrev = true;
		}

		drawList->PopClipRect();
	}

	// 軸と色と通常キーを描く
	void DrawKeys(const ImRect& rect, std::span<Engine::CurveChannel> channels, const Engine::CurveEditorState& state,
		std::span<Engine::CurveQuaternionAxisKey> quaternionAxisKeys) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		if (IsColorCurveSet(channels)) {
			if (state.IsChannelVisible(0)) {
				const size_t rgbKeyCount = Engine::AnimationChannelUtility::GetSharedKeyCount(channels.first(3));
				for (uint32_t keyIndex = 0; keyIndex < rgbKeyCount; ++keyIndex) {
					const float time = channels[0].keys[keyIndex].time;
					const Engine::Color4 color = EvaluateCurveColorAtTime(channels, time);
					const float previewValue = (color.r + color.g + color.b) / 3.0f;
					const ImVec2 pos = WorldToScreen(rect, state, time, previewValue);
					const bool selected = IsKeySelected(state, 0, keyIndex);
					const float radius = selected ? kCurveKeyRadius + 2.0f : kCurveKeyRadius + 1.0f;
					drawList->AddRectFilled(
						ImVec2(pos.x - radius, pos.y - radius), ImVec2(pos.x + radius, pos.y + radius), ToImU32(color), 2.0f);
					drawList->AddRect(ImVec2(pos.x - radius, pos.y - radius), ImVec2(pos.x + radius, pos.y + radius),
						selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(18, 18, 18, 255), 2.0f, 0, 1.5f);
				}
			}
			if (HasAlphaChannel(channels) && state.IsChannelVisible(3)) {
				const Engine::CurveChannel& channel = channels[3];
				const ImU32 color = ToImU32(channel.displayColor);
				for (uint32_t keyIndex = 0; keyIndex < channel.keys.size(); ++keyIndex) {
					const Engine::CurveKey& key = channel.keys[keyIndex];
					const ImVec2 pos = WorldToScreen(rect, state, key.time, key.value);
					const bool selected = IsKeySelected(state, 3, keyIndex);
					const ImU32 fillColor = selected ? IM_COL32(255, 255, 255, 255) : color;
					const ImU32 borderColor = selected ? color : IM_COL32(12, 12, 12, 255);
					drawList->AddCircleFilled(pos, selected ? kCurveKeyRadius + 1.5f : kCurveKeyRadius, fillColor, 12);
					drawList->AddCircle(pos, selected ? kCurveKeyRadius + 1.5f : kCurveKeyRadius, borderColor, 12, 1.5f);
				}
			}
			drawList->PopClipRect();
			return;
		}

		if (IsQuaternionCurveSet(channels)) {
			if (state.IsChannelVisible(0)) {
				for (uint32_t keyIndex = 0; keyIndex < channels[0].keys.size(); ++keyIndex) {
					const Engine::CurveQuaternionAxisKey axisKey = GetQuaternionAxisKey(channels, quaternionAxisKeys, keyIndex);
					const Engine::CurveKey& axisChannelKey = channels[0].keys[keyIndex];
					const ImVec2 pos = WorldToScreen(rect, state, axisChannelKey.time, axisChannelKey.value);
					const bool selected = IsKeySelected(state, 0, keyIndex);
					const float radius = selected ? kCurveKeyRadius + 2.0f : kCurveKeyRadius + 1.0f;
					drawList->AddRectFilled(ImVec2(pos.x - radius, pos.y - radius), ImVec2(pos.x + radius, pos.y + radius),
						GetAxisColor(axisKey), 2.0f);
					drawList->AddRect(ImVec2(pos.x - radius, pos.y - radius), ImVec2(pos.x + radius, pos.y + radius),
						selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(18, 18, 18, 255), 2.0f, 0, 1.5f);
				}
			}
			if (state.IsChannelVisible(1)) {
				const Engine::CurveChannel& channel = channels[1];
				const ImU32 color = ToImU32(channel.displayColor);
				for (uint32_t keyIndex = 0; keyIndex < channel.keys.size(); ++keyIndex) {
					const Engine::CurveKey& key = channel.keys[keyIndex];
					const ImVec2 pos = WorldToScreen(rect, state, key.time, key.value);
					const bool selected = IsKeySelected(state, 1, keyIndex);
					const ImU32 fillColor = selected ? IM_COL32(255, 255, 255, 255) : color;
					const ImU32 borderColor = selected ? color : IM_COL32(12, 12, 12, 255);
					drawList->AddCircleFilled(pos, selected ? kCurveKeyRadius + 1.5f : kCurveKeyRadius, fillColor, 12);
					drawList->AddCircle(pos, selected ? kCurveKeyRadius + 1.5f : kCurveKeyRadius, borderColor, 12, 1.5f);
				}
			}
			drawList->PopClipRect();
			return;
		}

		for (uint32_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
			if (!state.IsChannelVisible(channelIndex)) {
				continue;
			}

			const Engine::CurveChannel& channel = channels[channelIndex];
			const ImU32 color = ToImU32(channel.displayColor);

			for (uint32_t keyIndex = 0; keyIndex < channel.keys.size(); ++keyIndex) {
				const Engine::CurveKey& key = channel.keys[keyIndex];
				const ImVec2 pos = WorldToScreen(rect, state, key.time, key.value);
				const bool selected = IsKeySelected(state, channelIndex, keyIndex);
				const ImU32 fillColor = selected ? IM_COL32(255, 255, 255, 255) : color;
				const ImU32 borderColor = selected ? color : IM_COL32(12, 12, 12, 255);
				drawList->AddCircleFilled(pos, selected ? kCurveKeyRadius + 1.5f : kCurveKeyRadius, fillColor, 12);
				drawList->AddCircle(pos, selected ? kCurveKeyRadius + 1.5f : kCurveKeyRadius, borderColor, 12, 1.5f);
			}
		}

		drawList->PopClipRect();
	}

	// 現在時刻の線を描く
	void DrawCurrentTimeLine(const ImRect& rect, const Engine::CurveEditorState& state) {

		if (state.currentTime < state.visibleTimeMin || state.visibleTimeMax < state.currentTime) {
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		const ImVec2 p0 = WorldToScreen(rect, state, state.currentTime, state.visibleValueMin);
		const ImVec2 p1 = WorldToScreen(rect, state, state.currentTime, state.visibleValueMax);
		drawList->AddLine(ImVec2(p0.x, rect.Min.y), ImVec2(p1.x, rect.Max.y), IM_COL32(255, 210, 80, 255), 2.0f);

		drawList->PopClipRect();
	}
} // Engine::CurveEditorUtility

void Engine::MyGUI::CurveColorGradientBar(std::span<const CurveChannel> channels, float timeMin, float timeMax, bool hasAlpha) {

	if (channels.size() < 3) {
		return;
	}

	const float barHeight = 18.0f;
	const float barWidth = ImGui::GetContentRegionAvail().x;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImDrawList* drawList = ImGui::GetWindowDrawList();

	timeMax = (std::max)(timeMax, timeMin + 0.001f);
	constexpr int kSegments = 64;
	const float segWidth = barWidth / static_cast<float>(kSegments);

	// 時間軸に合わせて区間ごとに色を評価しグラデーションでつなぐ
	auto sampleColor = [&](float ratio) {
		const float time = timeMin + (timeMax - timeMin) * ratio;
		const float r = channels[0].Evaluate(time);
		const float g = channels[1].Evaluate(time);
		const float b = channels[2].Evaluate(time);
		const float a = hasAlpha && 3 < channels.size() ? channels[3].Evaluate(time) : 1.0f;
		return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
	};
	for (int s = 0; s < kSegments; ++s) {

		const ImU32 left = sampleColor(static_cast<float>(s) / kSegments);
		const ImU32 right = sampleColor(static_cast<float>(s + 1) / kSegments);
		const ImVec2 p0(origin.x + segWidth * static_cast<float>(s), origin.y);
		const ImVec2 p1(origin.x + segWidth * static_cast<float>(s + 1), origin.y + barHeight);
		drawList->AddRectFilledMultiColor(p0, p1, left, right, right, left);
	}
	ImGui::Dummy(ImVec2(barWidth, barHeight));
}
