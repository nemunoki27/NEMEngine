#include "MyGUI.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Utility/Enum/EnumAdapter.h>

// imgui
#include <imgui_internal.h>
// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <vector>

//============================================================================
//	MyGUI Curve classMethods
//============================================================================

namespace {

	// 上部ボタンサイズ
	constexpr ImVec2 kCurveToolbarItemSize = ImVec2(80.0f, 20.0f);
	// 左チャンネル一覧の幅
	constexpr float kCurveSidePanelWidth = 64.0f;
	// 右キーインスペクタの幅
	constexpr float kCurveInspectorWidth = 160.0f;
	// 右キーインスペクタのアイテムサイズ
	constexpr float kCurveInspectorItemWidth = 96.0f;
	// キー点の表示半径
	constexpr float kCurveKeyRadius = 4.0f;
	// カーブエディター内で共通の文字スケール
	constexpr float kCurveEditorFontScale = 0.72f;
	// 主グリッドとして扱う間隔
	constexpr int32_t kCurveGridMajorInterval = 8;
	// ルーラー領域
	constexpr float kCurveTopRulerHeight = 22.0f;
	constexpr float kCurveLeftRulerWidth = 52.0f;
	// 軸線色
	constexpr ImU32 kCurveAxisColor = IM_COL32(255, 210, 80, 255);

	float ClampVisibleTimeMin(float v) {
		return (std::max)(0.0f, v);
	}

	float SafePixelsPerSecond(float v) {
		return (std::clamp)(v, 32.0f, 512.0f);
	}

	float SafePixelsPerValue(float v) {
		return (std::clamp)(v, 24.0f, 320.0f);
	}

	float NiceStep(float rawStep) {

		if (rawStep <= 0.0f) {
			return 0.1f;
		}

		const float exponent = std::floor(std::log10(rawStep));
		const float base = std::pow(10.0f, exponent);
		const float fraction = rawStep / base;

		float niceFraction = 1.0f;
		if (fraction <= 1.0f)       niceFraction = 1.0f;
		else if (fraction <= 2.0f)  niceFraction = 2.0f;
		else if (fraction <= 5.0f)  niceFraction = 5.0f;
		else                        niceFraction = 10.0f;

		return niceFraction * base;
	}
	// 0に近い表示範囲で除算が壊れないようにする
	float SafeRange(float minValue, float maxValue) {

		const float range = maxValue - minValue;
		return std::abs(range) <= 0.00001f ? 1.0f : range;
	}

	// Time方向のズームやパンを反映する。
	// visibleValueMin / visibleValueMax はここでは触らず、
	// 左上のInputFloatでユーザーが決めた値を維持する。
	void UpdateHorizontalViewRange(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.pixelsPerSecond = SafePixelsPerSecond(state.pixelsPerSecond);

		const float graphW = (std::max)(1.0f, graphRect.GetWidth());
		const float timeRange = graphW / state.pixelsPerSecond;

		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = (std::max)(state.visibleTimeMin + timeRange, state.visibleTimeMin + 0.001f);

		const float desiredTimeStep = 80.0f / state.pixelsPerSecond; // 約80pxごと
		state.gridTimeStep = NiceStep(desiredTimeStep);
	}
	// 値方向の表示レンジからズーム係数を再構築する。
	// MaxValueを固定したままMinValueだけ動かした場合でも、
	// 次の入力で見た目が急変しないように内部状態を揃える。
	void UpdateVerticalZoomFromRange(const ImRect& graphRect, Engine::CurveEditorState& state) {

		state.visibleValueMax = (std::max)(state.visibleValueMax, state.visibleValueMin + 0.001f);

		const float graphH = (std::max)(1.0f, graphRect.GetHeight());
		const float valueRange = SafeRange(state.visibleValueMin, state.visibleValueMax);

		state.pixelsPerValue = SafePixelsPerValue(graphH / valueRange);

		const float desiredValueStep = 40.0f / state.pixelsPerValue;
		state.gridValueStep = NiceStep(desiredValueStep);
	}


	// キーの値を現在の表示範囲へ収める。
	// ドラッグ操作でもInspector操作でも同じ上限・下限を使う。
	float ClampKeyValueToVisibleRange(const Engine::CurveEditorState& state, float value) {

		return (std::clamp)(value, state.visibleValueMin, state.visibleValueMax);
	}

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
	void ZoomTimeAroundMouse(const ImRect& graphRect, Engine::CurveEditorState& state,
		const ImVec2& mousePos, float wheelDelta) {

		const float oldPps = state.pixelsPerSecond;
		const float newPps = SafePixelsPerSecond(oldPps * (wheelDelta > 0.0f ? 1.15f : (1.0f / 1.15f)));

		const float mouseRatioX = (mousePos.x - graphRect.Min.x) / (std::max)(1.0f, graphRect.GetWidth());
		const float worldTime = state.visibleTimeMin + (state.visibleTimeMax - state.visibleTimeMin) * mouseRatioX;

		state.pixelsPerSecond = newPps;
		const float newRange = graphRect.GetWidth() / state.pixelsPerSecond;
		state.visibleTimeMin = worldTime - newRange * mouseRatioX;
		state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
		state.visibleTimeMax = state.visibleTimeMin + newRange;
	}

	// 背景上で「Add Key」したときに、
	// クリック位置に最も近い表示中チャンネルへ追加する。
	// これにより Vector2/3 で Y を編集しているときに X へ入ってしまう問題を避ける。
	bool FindBestChannelForAddKey(std::span<Engine::CurveChannel> channels,
		const Engine::CurveEditorState& state, float time, float value, uint32_t& outChannelIndex) {

		float bestDistance = (std::numeric_limits<float>::max)();
		bool found = false;

		for (uint32_t i = 0; i < channels.size(); ++i) {
			if (!state.IsChannelVisible(i)) {
				continue;
			}

			const Engine::CurveChannel& channel = channels[i];
			const float sampledValue = channel.Evaluate(time);
			const float distance = std::abs(sampledValue - value);
			if (distance < bestDistance) {
				bestDistance = distance;
				outChannelIndex = i;
				found = true;
			}
		}

		if (found) {
			return true;
		}

		for (uint32_t i = 0; i < channels.size(); ++i) {
			if (state.IsChannelVisible(i)) {
				outChannelIndex = i;
				return true;
			}
		}
		return false;
	}

	// Color4をImGuiの描画色へ変換する
	ImU32 ToImU32(const Engine::Color4& color) {

		return ImGui::ColorConvertFloat4ToU32(ImVec4(color.r, color.g, color.b, color.a));
	}
	// スナップが有効な場合だけ時間を指定間隔へ丸める
	float SnapTime(float time, bool enableSnap, float interval) {

		if (!enableSnap || interval <= 0.0f) {
			return time;
		}
		return std::round(time / interval) * interval;
	}
	// グリッド表示で-0.00が出ないように丸める
	float NormalizeGridValue(float value) {

		return std::abs(value) <= 0.00001f ? 0.0f : value;
	}
	// グリッド間隔に合わせて表示桁数を決める
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
		return ImVec2(
			rect.Min.x + tx * rect.GetWidth(),
			rect.Max.y - ty * rect.GetHeight());
	}
	// 画面座標をカーブ座標へ変換する
	ImVec2 ScreenToWorld(const ImRect& rect, const Engine::CurveEditorState& state, const ImVec2& pos) {

		const float tx = (pos.x - rect.Min.x) / (std::max)(1.0f, rect.GetWidth());
		const float ty = (rect.Max.y - pos.y) / (std::max)(1.0f, rect.GetHeight());
		return ImVec2(
			state.visibleTimeMin + tx * (state.visibleTimeMax - state.visibleTimeMin),
			state.visibleValueMin + ty * (state.visibleValueMax - state.visibleValueMin));
	}
	// ImRect::Contains相当を明示的に扱う
	bool RectContains(const ImRect& rect, const ImVec2& pos) {

		return rect.Min.x <= pos.x && pos.x <= rect.Max.x && rect.Min.y <= pos.y && pos.y <= rect.Max.y;
	}
	// toolbar itemの縦位置が揃うようにFramePaddingを一時的に調整する
	void PushToolbarItemStyle() {

		const float fontSize = ImGui::GetFontSize();
		const float paddingY = (std::max)(0.0f, (kCurveToolbarItemSize.y - fontSize) * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, paddingY));
	}
	// toolbar用のボタンを描画する
	bool DrawToolbarButton(const char* label) {

		PushToolbarItemStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
		const bool clicked = ImGui::Button(label, kCurveToolbarItemSize);
		ImGui::PopStyleVar(2);
		return clicked;
	}
	// toolbar用のfloat入力を描画する
	bool DrawToolbarDragFloat(const char* label, float& value, float speed, float minValue, float maxValue, const char* format) {

		PushToolbarItemStyle();
		ImGui::SetNextItemWidth(kCurveToolbarItemSize.x);
		const bool changed = ImGui::DragFloat(label, &value, speed, minValue, maxValue, format);
		ImGui::PopStyleVar();
		return changed;
	}

	bool IsColorChannelSet(std::span<Engine::CurveChannel> channels) {

		if (channels.size() != 3 && channels.size() != 4) {
			return false;
		}
		if (channels[0].name != "R" || channels[1].name != "G" || channels[2].name != "B") {
			return false;
		}
		return channels.size() == 3 || channels[3].name == "A";
	}

	void DrawToolbarCurrentValues(std::span<Engine::CurveChannel> channels, const Engine::CurveEditorState& state) {

		if (channels.empty()) {
			return;
		}

		ImGui::SameLine();
		ImGui::TextUnformatted("=");

		if (IsColorChannelSet(channels)) {
			float r = channels[0].Evaluate(state.currentTime);
			float g = channels[1].Evaluate(state.currentTime);
			float b = channels[2].Evaluate(state.currentTime);
			float a = channels.size() >= 4 ? channels[3].Evaluate(state.currentTime) : 1.0f;
			r = (std::clamp)(r, 0.0f, 1.0f);
			g = (std::clamp)(g, 0.0f, 1.0f);
			b = (std::clamp)(b, 0.0f, 1.0f);
			a = (std::clamp)(a, 0.0f, 1.0f);

			ImGui::SameLine(0.0f, 6.0f);
			ImGui::ColorButton("##CurrentColorAtTime",
				ImVec4(r, g, b, a),
				ImGuiColorEditFlags_NoTooltip,
				ImVec2(kCurveToolbarItemSize.y, kCurveToolbarItemSize.y));

			for (uint32_t i = 0; i < channels.size(); ++i) {
				const Engine::CurveChannel& channel = channels[i];
				const float value = channel.Evaluate(state.currentTime);
				ImGui::SameLine(0.0f, 6.0f);
				ImGui::TextColored(
					ImVec4(channel.displayColor.r, channel.displayColor.g, channel.displayColor.b, channel.displayColor.a),
					"%s: %.3f",
					channel.name.c_str(),
					value);
			}
			return;
		}

		for (uint32_t i = 0; i < channels.size(); ++i) {
			const Engine::CurveChannel& channel = channels[i];
			const float value = channel.Evaluate(state.currentTime);
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextColored(
				ImVec4(channel.displayColor.r, channel.displayColor.g, channel.displayColor.b, channel.displayColor.a),
				"%s: %.3f",
				channel.name.c_str(),
				value);
		}
	}
	// toolbar用のチェックボックスを正方形で描画する
	bool DrawToolbarCheckbox(const char* label, bool& value) {

		ImGui::PushID(label);

		const ImVec2 squareSize(kCurveToolbarItemSize.y, kCurveToolbarItemSize.y);
		const ImVec2 squareMin = ImGui::GetCursorScreenPos();
		const ImVec2 squareMax(squareMin.x + squareSize.x, squareMin.y + squareSize.y);
		const bool clicked = ImGui::InvisibleButton("##Check", squareSize);
		if (clicked) {
			value = !value;
		}

		const bool hovered = ImGui::IsItemHovered();
		const ImU32 frameColor = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
		const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_Border);
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(squareMin, squareMax, frameColor, ImGui::GetStyle().FrameRounding);
		drawList->AddRect(squareMin, squareMax, borderColor, ImGui::GetStyle().FrameRounding);
		if (value) {
			const float checkSize = kCurveToolbarItemSize.y * 0.65f;
			const ImVec2 checkPos(
				squareMin.x + (kCurveToolbarItemSize.y - checkSize) * 0.5f,
				squareMin.y + (kCurveToolbarItemSize.y - checkSize) * 0.5f);
			ImGui::RenderCheckMark(drawList, checkPos, ImGui::GetColorU32(ImGuiCol_CheckMark), checkSize);
		}

		ImGui::SameLine(0.0f, 4.0f);
		const float textY = ImGui::GetCursorPosY();
		ImGui::SetCursorPosY(textY + (kCurveToolbarItemSize.y - ImGui::GetTextLineHeight()) * 0.5f);
		ImGui::TextUnformatted(label);
		ImGui::SetCursorPosY(textY);

		ImGui::PopID();
		return clicked;
	}
	// ドラッグ方向に関わらず矩形のmin/maxを整える
	void NormalizeRect(ImVec2& minPos, ImVec2& maxPos) {

		if (maxPos.x < minPos.x) {
			std::swap(minPos.x, maxPos.x);
		}
		if (maxPos.y < minPos.y) {
			std::swap(minPos.y, maxPos.y);
		}
	}
	// 表示中チャンネルのキー範囲にビューを合わせる
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

		// Frameでは値方向の表示範囲は変更しない。
		// MinValue / MaxValue はユーザーが決めた値をそのまま維持する。

		// 実際のグラフサイズからズーム係数を同期する
		const float graphW = (std::max)(1.0f, graphRect.GetWidth());

		state.pixelsPerSecond = SafePixelsPerSecond(graphW / SafeRange(state.visibleTimeMin, state.visibleTimeMax));
		state.gridTimeStep = NiceStep(80.0f / state.pixelsPerSecond);

		// Frame直後の見た目と、その後の通常操作の基準を揃える。
		UpdateVerticalZoomFromRange(graphRect, state);
	}

	// Color3 / Color4 を RGB 代表キー + Alpha キーとして扱うための判定群
	bool IsColorCurveSet(std::span<Engine::CurveChannel> channels) {

		if (channels.size() != 3 && channels.size() != 4) {

			return false;

		}

		return channels[0].name == "R" && channels[1].name == "G" && channels[2].name == "B";

	}

	bool HasAlphaChannel(std::span<Engine::CurveChannel> channels) {

		return IsColorCurveSet(channels) && channels.size() == 4 && channels[3].name == "A";

	}

	bool IsRgbSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection) {

		return IsColorCurveSet(channels) && selection.channelIndex == 0 &&

			selection.keyIndex < channels[0].keys.size() &&

			selection.keyIndex < channels[1].keys.size() &&

			selection.keyIndex < channels[2].keys.size();

	}

	bool IsAlphaSelection(std::span<Engine::CurveChannel> channels, const Engine::CurveKeySelection& selection) {

		return HasAlphaChannel(channels) && selection.channelIndex == 3 && selection.keyIndex < channels[3].keys.size();

	}

	Engine::Color4 EvaluateCurveColorAtTime(std::span<Engine::CurveChannel> channels, float time) {

		const float r = channels[0].Evaluate(time);

		const float g = channels[1].Evaluate(time);

		const float b = channels[2].Evaluate(time);

		const float a = HasAlphaChannel(channels) ? channels[3].Evaluate(time) : 1.0f;

		return Engine::Color4(

			(std::clamp)(r, 0.0f, 1.0f),

			(std::clamp)(g, 0.0f, 1.0f),

			(std::clamp)(b, 0.0f, 1.0f),

			(std::clamp)(a, 0.0f, 1.0f));

	}

	uint32_t AddColorRgbKey(std::span<Engine::CurveChannel> channels, float time) {

		const Engine::Color4 color = EvaluateCurveColorAtTime(channels, time);

		const uint32_t index = channels[0].AddKey(time, color.r);

		channels[1].AddKey(time, color.g);

		channels[2].AddKey(time, color.b);

		return index;

	}

	bool RemoveColorRgbKey(std::span<Engine::CurveChannel> channels, uint32_t keyIndex) {

		bool removed = false;

		removed |= channels[0].RemoveKey(keyIndex);

		removed |= channels[1].RemoveKey(keyIndex);

		removed |= channels[2].RemoveKey(keyIndex);

		return removed;

	}

	void SortColorRgbKeys(std::span<Engine::CurveChannel> channels) {

		channels[0].SortKeys();

		channels[1].SortKeys();

		channels[2].SortKeys();

	}

	// 指定キーが選択状態かを返す
	bool IsKeySelected(const Engine::CurveEditorState& state, uint32_t channelIndex, uint32_t keyIndex) {

		return state.IsSelected(channelIndex, keyIndex);
	}
	// マウス位置に最も近いキーを探す
	bool HitTestKey(const ImRect& rect, std::span<Engine::CurveChannel> channels,
		const Engine::CurveEditorState& state, const ImVec2& mouse,
		Engine::CurveKeySelection& outSelection) {

		float bestDistanceSq = kCurveKeyRadius * kCurveKeyRadius * 4.0f;
		bool found = false;

		if (IsColorCurveSet(channels)) {
			if (state.IsChannelVisible(0)) {
				const uint32_t rgbKeyCount = (std::min)({
					static_cast<uint32_t>(channels[0].keys.size()),
					static_cast<uint32_t>(channels[1].keys.size()),
					static_cast<uint32_t>(channels[2].keys.size()) });
				for (uint32_t keyIndex = 0; keyIndex < rgbKeyCount; ++keyIndex) {
					const float time = channels[0].keys[keyIndex].time;
					const Engine::Color4 color = EvaluateCurveColorAtTime(channels, time);
					const float previewValue = (color.r + color.g + color.b) / 3.0f;
					const ImVec2 keyPos = WorldToScreen(rect, state, time, previewValue);
					const float dx = keyPos.x - mouse.x;
					const float dy = keyPos.y - mouse.y;
					const float distanceSq = dx * dx + dy * dy;
					if (distanceSq <= bestDistanceSq) {
						bestDistanceSq = distanceSq;
						outSelection = { 0, keyIndex };
						found = true;
					}
				}
			}

			if (HasAlphaChannel(channels) && state.IsChannelVisible(3)) {
				for (uint32_t keyIndex = 0; keyIndex < channels[3].keys.size(); ++keyIndex) {
					const Engine::CurveKey& key = channels[3].keys[keyIndex];
					const ImVec2 keyPos = WorldToScreen(rect, state, key.time, key.value);
					const float dx = keyPos.x - mouse.x;
					const float dy = keyPos.y - mouse.y;
					const float distanceSq = dx * dx + dy * dy;
					if (distanceSq <= bestDistanceSq) {
						bestDistanceSq = distanceSq;
						outSelection = { 3, keyIndex };
						found = true;
					}
				}
			}
			return found;
		}

		for (uint32_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
			if (!state.IsChannelVisible(channelIndex)) {
				continue;
			}

			const Engine::CurveChannel& channel = channels[channelIndex];
			for (uint32_t keyIndex = 0; keyIndex < channel.keys.size(); ++keyIndex) {
				const Engine::CurveKey& key = channel.keys[keyIndex];
				const ImVec2 keyPos = WorldToScreen(rect, state, key.time, key.value);
				const float dx = keyPos.x - mouse.x;
				const float dy = keyPos.y - mouse.y;
				const float distanceSq = dx * dx + dy * dy;
				if (distanceSq <= bestDistanceSq) {
					bestDistanceSq = distanceSq;
					outSelection = { channelIndex, keyIndex };
					found = true;
				}
			}
		}
		return found;
	}
	// 背景グリッドと軸ラベルを描画する
	void DrawGrid(const ImRect& rect, const Engine::CurveEditorState& state) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();

		const ImU32 backgroundColor = IM_COL32(5, 5, 5, 255);
		const ImU32 minorColor = IM_COL32(38, 38, 42, 180);

		drawList->AddRectFilled(rect.Min, rect.Max, backgroundColor);

		const float timeStep = (std::max)(0.0001f, state.gridTimeStep);
		const float valueStep = (std::max)(0.0001f, state.gridValueStep);

		// Time 0 より左は出さない
		const float timeBegin = std::ceil((std::max)(0.0f, state.visibleTimeMin) / timeStep) * timeStep;
		for (float time = timeBegin; time <= state.visibleTimeMax + timeStep * 0.5f; time += timeStep) {
			const ImVec2 pos = WorldToScreen(rect, state, time, state.visibleValueMin);
			drawList->AddLine(ImVec2(pos.x, rect.Min.y), ImVec2(pos.x, rect.Max.y), minorColor);
		}

		const float valueBegin = std::ceil(state.visibleValueMin / valueStep) * valueStep;
		for (float value = valueBegin; value <= state.visibleValueMax + valueStep * 0.5f; value += valueStep) {
			const ImVec2 pos = WorldToScreen(rect, state, state.visibleTimeMin, value);
			drawList->AddLine(ImVec2(rect.Min.x, pos.y), ImVec2(rect.Max.x, pos.y), minorColor);
		}

		// Time = 0 の黄色線
		if (0.0f >= state.visibleTimeMin && 0.0f <= state.visibleTimeMax) {
			const ImVec2 p0 = WorldToScreen(rect, state, 0.0f, state.visibleValueMin);
			const ImVec2 p1 = WorldToScreen(rect, state, 0.0f, state.visibleValueMax);
			drawList->AddLine(ImVec2(p0.x, rect.Min.y), ImVec2(p1.x, rect.Max.y), kCurveAxisColor, 2.0f);
		}

		// Value = 0 の黄色線
		if (0.0f >= state.visibleValueMin && 0.0f <= state.visibleValueMax) {
			const ImVec2 p0 = WorldToScreen(rect, state, state.visibleTimeMin, 0.0f);
			const ImVec2 p1 = WorldToScreen(rect, state, state.visibleTimeMax, 0.0f);
			drawList->AddLine(ImVec2(rect.Min.x, p0.y), ImVec2(rect.Max.x, p1.y), kCurveAxisColor, 2.0f);
		}
	}
	void DrawTimeRuler(const ImRect& rect, const Engine::CurveEditorState& state) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(8, 8, 8, 255));
		drawList->AddRect(rect.Min, rect.Max, IM_COL32(80, 80, 80, 255));

		const ImU32 textColor = IM_COL32(210, 210, 210, 255);
		const ImU32 tickColor = IM_COL32(110, 110, 116, 255);

		const float timeStep = (std::max)(0.0001f, state.gridTimeStep);
		const float timeBegin = std::ceil((std::max)(0.0f, state.visibleTimeMin) / timeStep) * timeStep;

		for (float time = timeBegin; time <= state.visibleTimeMax + timeStep * 0.5f; time += timeStep) {
			const int32_t lineIndex = static_cast<int32_t>(std::round(time / timeStep));
			const bool major = (lineIndex % kCurveGridMajorInterval) == 0;

			const ImVec2 pos = WorldToScreen(ImRect(
				ImVec2(rect.Min.x, rect.Min.y),
				ImVec2(rect.Max.x, rect.Max.y)), state, time, state.visibleValueMin);

			const float tickH = major ? 10.0f : 6.0f;
			drawList->AddLine(ImVec2(pos.x, rect.Max.y - tickH), ImVec2(pos.x, rect.Max.y), tickColor);

			// 目盛りがある位置にはすべて値を表示する
			const std::string text = FormatGridValue(time, timeStep);
			drawList->AddText(ImVec2(pos.x + 3.0f, rect.Min.y + 3.0f), textColor, text.c_str());
		}
	}

	void DrawValueRuler(const ImRect& rect, const Engine::CurveEditorState& state) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(8, 8, 8, 255));
		drawList->AddRect(rect.Min, rect.Max, IM_COL32(80, 80, 80, 255));

		const ImU32 textColor = IM_COL32(210, 210, 210, 255);
		const ImU32 tickColor = IM_COL32(110, 110, 116, 255);

		const float valueStep = (std::max)(0.0001f, state.gridValueStep);
		const float valueBegin = std::ceil(state.visibleValueMin / valueStep) * valueStep;

		for (float value = valueBegin; value <= state.visibleValueMax + valueStep * 0.5f; value += valueStep) {
			const int32_t lineIndex = static_cast<int32_t>(std::round(value / valueStep));
			const bool major = (lineIndex % kCurveGridMajorInterval) == 0;

			const ImVec2 pos = WorldToScreen(
				ImRect(ImVec2(rect.Min.x, rect.Min.y), ImVec2(rect.Max.x, rect.Max.y)),
				state,
				state.visibleTimeMin,
				value);

			const float tickW = major ? 10.0f : 6.0f;
			drawList->AddLine(ImVec2(rect.Max.x - tickW, pos.y), ImVec2(rect.Max.x, pos.y), tickColor);

			// 目盛りがある位置にはすべて値を表示する
			const std::string text = FormatGridValue(value, valueStep);
			const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());

			ImVec2 textPos(rect.Max.x - textSize.x - 4.0f, pos.y - textSize.y * 0.5f);

			// ルーラー矩形の中に収める
			textPos.x = (std::clamp)(textPos.x, rect.Min.x + 2.0f, rect.Max.x - textSize.x - 2.0f);
			textPos.y = (std::clamp)(textPos.y, rect.Min.y + 1.0f, rect.Max.y - textSize.y - 1.0f);

			drawList->AddText(textPos, textColor, text.c_str());
		}

		drawList->PopClipRect();
	}
	// 評価値をサンプリングしてカーブ線を描画する
	void DrawCurveSamples(const ImRect& rect, const Engine::CurveChannel& channel,
		const Engine::CurveEditorState& state) {

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
	// Color3 / Color4 の RGB 代表キーを線で結ぶ
	void DrawColorRgbCurveSamples(const ImRect& rect, std::span<Engine::CurveChannel> channels,
		const Engine::CurveEditorState& state) {

		if (!IsColorCurveSet(channels) || !state.IsChannelVisible(0)) {
			return;
		}

		const uint32_t rgbKeyCount = (std::min)({
			static_cast<uint32_t>(channels[0].keys.size()),
			static_cast<uint32_t>(channels[1].keys.size()),
			static_cast<uint32_t>(channels[2].keys.size()) });
		if (rgbKeyCount == 0) {
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		ImVec2 prevPos{};
		ImU32 prevColor = 0;
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
			prevColor = lineColor;
			hasPrev = true;
		}

		drawList->PopClipRect();
	}

	// キー点を描画する
	void DrawKeys(const ImRect& rect, std::span<Engine::CurveChannel> channels,
		const Engine::CurveEditorState& state) {

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		if (IsColorCurveSet(channels)) {
			if (state.IsChannelVisible(0)) {
				const uint32_t rgbKeyCount = (std::min)({
					static_cast<uint32_t>(channels[0].keys.size()),
					static_cast<uint32_t>(channels[1].keys.size()),
					static_cast<uint32_t>(channels[2].keys.size()) });
				for (uint32_t keyIndex = 0; keyIndex < rgbKeyCount; ++keyIndex) {
					const float time = channels[0].keys[keyIndex].time;
					const Engine::Color4 color = EvaluateCurveColorAtTime(channels, time);
					const float previewValue = (color.r + color.g + color.b) / 3.0f;
					const ImVec2 pos = WorldToScreen(rect, state, time, previewValue);
					const bool selected = IsKeySelected(state, 0, keyIndex);
					const float radius = selected ? kCurveKeyRadius + 2.0f : kCurveKeyRadius + 1.0f;
					drawList->AddRectFilled(ImVec2(pos.x - radius, pos.y - radius), ImVec2(pos.x + radius, pos.y + radius), ToImU32(color), 2.0f);
					drawList->AddRect(ImVec2(pos.x - radius, pos.y - radius), ImVec2(pos.x + radius, pos.y + radius), selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(18, 18, 18, 255), 2.0f, 0, 1.5f);
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
	// 現在時刻カーソルを描画する
	void DrawCurrentTimeLine(const ImRect& rect, const Engine::CurveEditorState& state) {

		if (state.currentTime < state.visibleTimeMin || state.visibleTimeMax < state.currentTime) {
			return;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(rect.Min, rect.Max, true);

		const ImVec2 p0 = WorldToScreen(rect, state, state.currentTime, state.visibleValueMin);
		const ImVec2 p1 = WorldToScreen(rect, state, state.currentTime, state.visibleValueMax);
		drawList->AddLine(ImVec2(p0.x, rect.Min.y), ImVec2(p1.x, rect.Max.y),
			IM_COL32(255, 210, 80, 255), 2.0f);

		drawList->PopClipRect();
	}
	// 選択中キーをまとめて削除する
	void DeleteSelectedKeys(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state) {

		// 後ろから削除できるようにチャンネル/キーを降順に並べる
		std::sort(state.selectedKeys.begin(), state.selectedKeys.end(),
			[](const Engine::CurveKeySelection& lhs, const Engine::CurveKeySelection& rhs) {
				if (lhs.channelIndex != rhs.channelIndex) {
					return lhs.channelIndex > rhs.channelIndex;
				}
				return lhs.keyIndex > rhs.keyIndex;
			});
		// indexずれを避けながら削除する
		for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
			if (selection.channelIndex < channels.size()) {
				channels[selection.channelIndex].RemoveKey(selection.keyIndex);
			}
		}
		state.ClearSelection();
	}
	// 上部ツールバーを描画する
	void DrawToolbar(std::span<Engine::CurveChannel> channels,
		Engine::CurveEditorState& state,
		Engine::CurveEditResult& result) {

		const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
		ImGui::SetWindowFontScale(kCurveEditorFontScale);

		if (DrawToolbarButton("Frame")) {
			state.frameSelectionRequest = true;
			result.valueChanged = true;
		}
		ImGui::SameLine();

		DrawToolbarCheckbox("Snap", state.snapEnabled);
		ImGui::SameLine();
		DrawToolbarDragFloat("Step", state.snapInterval, 0.001f, 0.001f, 10.0f, "%.3f");
		ImGui::SameLine();
		DrawToolbarDragFloat("Time", state.currentTime, 0.01f, 0.0f, 10000.0f, "%.3f");
		DrawToolbarCurrentValues(channels, state);

		ImGui::SetWindowFontScale(previousFontScale);
	}
	// 左側のチャンネル一覧を描画する
	void DrawChannelPanel(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state, float height) {

		ImGui::BeginChild("##CurveChannels", ImVec2(kCurveSidePanelWidth, height), true);
		const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
		ImGui::SetWindowFontScale(kCurveEditorFontScale);

		ImGui::TextUnformatted("Channels");
		ImGui::Separator();

		// チャンネルごとに表示ON/OFFとキー数を出す
		for (uint32_t i = 0; i < channels.size(); ++i) {
			Engine::CurveChannel& channel = channels[i];
			bool visible = state.IsChannelVisible(i);
			ImGui::PushID(static_cast<int>(i));
			if (ImGui::ColorButton("##Color", ImVec4(channel.displayColor.r, channel.displayColor.g,
				channel.displayColor.b, channel.displayColor.a), ImGuiColorEditFlags_NoTooltip, ImVec2(14.0f, 14.0f))) {
			}
			ImGui::SameLine();
			if (ImGui::Checkbox(channel.name.c_str(), &visible)) {
				state.SetChannelVisible(i, visible);
			}
			ImGui::TextDisabled("%u keys", static_cast<uint32_t>(channel.keys.size()));
			ImGui::PopID();
		}
		ImGui::SetWindowFontScale(previousFontScale);
		ImGui::EndChild();
	}
	// 右側のキーインスペクタを描画する
	void DrawInspector(std::span<Engine::CurveChannel> channels,
		Engine::CurveEditorState& state, Engine::CurveEditResult& result, float height) {

		ImGui::BeginChild("##CurveInspector", ImVec2(kCurveInspectorWidth, height), true);
		const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
		ImGui::SetWindowFontScale(kCurveEditorFontScale);

		ImGui::TextUnformatted("Key");
		ImGui::Separator();
		if (state.selectedKeys.empty()) {
			ImGui::TextDisabled("No key selected");
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		Engine::CurveKeySelection selection = state.selectedKeys.front();

		if (IsColorCurveSet(channels) && IsRgbSelection(channels, selection)) {
			Engine::CurveKey& keyR = channels[0].keys[selection.keyIndex];
			Engine::CurveKey& keyG = channels[1].keys[selection.keyIndex];
			Engine::CurveKey& keyB = channels[2].keys[selection.keyIndex];
			ImGui::TextUnformatted("RGB");
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			float time = keyR.time;
			if (ImGui::DragFloat("Time", &time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
				keyR.time = time;
				keyG.time = time;
				keyB.time = time;
				result.valueChanged = true;
			}
			float color[3] = { keyR.value, keyG.value, keyB.value };
			if (ImGui::ColorEdit3("Color", color, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_PickerHueWheel)) {
				keyR.value = color[0];
				keyG.value = color[1];
				keyB.value = color[2];
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (Engine::EnumAdapter<Engine::CurveInterpolationMode>::Combo("Interp", &keyR.interpolation)) {
				keyG.interpolation = keyR.interpolation;
				keyB.interpolation = keyR.interpolation;
				result.valueChanged = true;
			}
			if (result.valueChanged) {
				SortColorRgbKeys(channels);
				result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			}
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		if (IsColorCurveSet(channels) && IsAlphaSelection(channels, selection)) {
			Engine::CurveChannel& channel = channels[3];
			Engine::CurveKey& key = channel.keys[selection.keyIndex];
			ImGui::TextUnformatted("Alpha");
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::DragFloat("Time", &key.time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (ImGui::DragFloat("Value", &key.value, 0.01f, 0.0f, 1.0f, "%.3f")) {
				result.valueChanged = true;
			}
			ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
			if (Engine::EnumAdapter<Engine::CurveInterpolationMode>::Combo("Interp", &key.interpolation)) {
				result.valueChanged = true;
			}
			if (result.valueChanged) {
				key.value = (std::clamp)(key.value, 0.0f, 1.0f);
				channel.SortKeys();
				result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
			}
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		if (channels.size() <= selection.channelIndex || channels[selection.channelIndex].keys.size() <= selection.keyIndex) {
			ImGui::TextDisabled("Invalid selection");
			ImGui::SetWindowFontScale(previousFontScale);
			ImGui::EndChild();
			return;
		}

		Engine::CurveChannel& channel = channels[selection.channelIndex];
		Engine::CurveKey& key = channel.keys[selection.keyIndex];
		ImGui::TextUnformatted(channel.name.c_str());
		ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
		if (ImGui::DragFloat("Time", &key.time, 0.01f, -10000.0f, 10000.0f, "%.3f")) {
			result.valueChanged = true;
		}
		ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
		if (ImGui::DragFloat("Value", &key.value, 0.01f, state.visibleValueMin, state.visibleValueMax, "%.3f")) {
			result.valueChanged = true;
		}
		ImGui::SetNextItemWidth(kCurveInspectorItemWidth);
		if (Engine::EnumAdapter<Engine::CurveInterpolationMode>::Combo("Interp", &key.interpolation)) {
			result.valueChanged = true;
		}
		if (result.valueChanged) {
			key.value = ClampKeyValueToVisibleRange(state, key.value);
			channel.SortKeys();
			result.editFinished |= ImGui::IsItemDeactivatedAfterEdit();
		}
		ImGui::SetWindowFontScale(previousFontScale);
		ImGui::EndChild();
	}
	// グラフ領域の入力を処理する
	void HandleGraphInput(const ImRect& graphRect, std::span<Engine::CurveChannel> channels,
		Engine::CurveEditorState& state,
		Engine::CurveEditResult& result) {

		ImGuiIO& io = ImGui::GetIO();
		const ImVec2 mouse = io.MousePos;
		const bool hovered = RectContains(graphRect, mouse);

		state.hasHoveredKey = hovered && HitTestKey(graphRect, channels, state, mouse, state.hoveredKey);

		if (hovered && io.MouseWheel != 0.0f) {
			ZoomTimeAroundMouse(graphRect, state, mouse, io.MouseWheel);
			UpdateHorizontalViewRange(graphRect, state);
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
			state.dragMode = Engine::CurveEditorDragMode::Pan;
			state.dragStartMouse = mouse;
			state.dragLastMouse = mouse;
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
			if (state.hasHoveredKey) {
				if (io.KeyCtrl) {
					state.ToggleSelection(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex);
				} else if (!state.IsSelected(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex)) {
					state.SelectSingle(state.hoveredKey.channelIndex, state.hoveredKey.keyIndex);
				}
				state.dragMode = Engine::CurveEditorDragMode::Key;
				result.selectionChanged = true;
			} else {
				if (!io.KeyCtrl) {
					state.ClearSelection();
					result.selectionChanged = true;
				}
				state.dragMode = Engine::CurveEditorDragMode::Marquee;
				state.marqueeActive = true;
				state.marqueeMin = mouse;
				state.marqueeMax = mouse;
			}
			state.dragStartMouse = mouse;
			state.dragLastMouse = mouse;
		}

		if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
			state.contextMenuWorld = ScreenToWorld(graphRect, state, mouse);
			state.contextMenuOnKey = state.hasHoveredKey;
			if (state.contextMenuOnKey) {
				state.contextMenuKey = state.hoveredKey;
			}
			ImGui::OpenPopup("##CurveContextMenu");
		}

		if (ImGui::BeginPopup("##CurveContextMenu")) {
			if (state.contextMenuOnKey) {
				if (ImGui::MenuItem("Delete Key")) {
					if (IsColorCurveSet(channels) && IsRgbSelection(channels, state.contextMenuKey)) {
						RemoveColorRgbKey(channels, state.contextMenuKey.keyIndex);
						result.valueChanged = true;
						result.selectionChanged = true;
						state.ClearSelection();
					} else if (IsColorCurveSet(channels) && IsAlphaSelection(channels, state.contextMenuKey)) {
						channels[3].RemoveKey(state.contextMenuKey.keyIndex);
						result.valueChanged = true;
						result.selectionChanged = true;
						state.ClearSelection();
					} else if (state.contextMenuKey.channelIndex < channels.size()) {
						Engine::CurveChannel& channel = channels[state.contextMenuKey.channelIndex];
						if (state.contextMenuKey.keyIndex < channel.keys.size()) {
							channel.RemoveKey(state.contextMenuKey.keyIndex);
							result.valueChanged = true;
							result.selectionChanged = true;
							state.ClearSelection();
						}
					}
				}
			} else {
				if (IsColorCurveSet(channels)) {
					if (ImGui::MenuItem("Add RGB Key")) {
						const float time = SnapTime((std::max)(0.0f, state.contextMenuWorld.x), state.snapEnabled, state.snapInterval);
						const uint32_t newKeyIndex = AddColorRgbKey(channels, time);
						state.ClearSelection();
						state.selectedKeys.push_back({ 0, newKeyIndex });
						result.valueChanged = true;
						result.selectionChanged = true;
					}
					if (HasAlphaChannel(channels) && ImGui::MenuItem("Add Alpha Key")) {
						const float time = SnapTime((std::max)(0.0f, state.contextMenuWorld.x), state.snapEnabled, state.snapInterval);
						const float alpha = (std::clamp)(channels[3].Evaluate(time), 0.0f, 1.0f);
						const uint32_t newKeyIndex = channels[3].AddKey(time, alpha);
						state.ClearSelection();
						state.selectedKeys.push_back({ 3, newKeyIndex });
						result.valueChanged = true;
						result.selectionChanged = true;
					}
				} else if (ImGui::MenuItem("Add Key")) {
					float time = SnapTime((std::max)(0.0f, state.contextMenuWorld.x), state.snapEnabled, state.snapInterval);
					float value = state.contextMenuWorld.y;
					uint32_t targetChannel = 0;
					if (FindBestChannelForAddKey(channels, state, time, value, targetChannel)) {
						Engine::CurveChannel& channel = channels[targetChannel];
						const uint32_t newKeyIndex = channel.AddKey(time, value);
						state.ClearSelection();
						state.selectedKeys.push_back({ targetChannel, newKeyIndex });
						result.valueChanged = true;
						result.selectionChanged = true;
					}
				}
			}
			ImGui::EndPopup();
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Pan && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
			const ImVec2 prevWorld = ScreenToWorld(graphRect, state, state.dragLastMouse);
			const ImVec2 nowWorld = ScreenToWorld(graphRect, state, mouse);
			const float dt = prevWorld.x - nowWorld.x;
			state.visibleTimeMin += dt;
			state.visibleTimeMin = ClampVisibleTimeMin(state.visibleTimeMin);
			UpdateHorizontalViewRange(graphRect, state);
			state.dragLastMouse = mouse;
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Key && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
			const ImVec2 prevWorld = ScreenToWorld(graphRect, state, state.dragLastMouse);
			const ImVec2 nowWorld = ScreenToWorld(graphRect, state, mouse);
			const float dt = nowWorld.x - prevWorld.x;
			const float dv = nowWorld.y - prevWorld.y;
			for (const Engine::CurveKeySelection& selection : state.selectedKeys) {
				if (IsColorCurveSet(channels) && IsRgbSelection(channels, selection)) {
					Engine::CurveKey& keyR = channels[0].keys[selection.keyIndex];
					Engine::CurveKey& keyG = channels[1].keys[selection.keyIndex];
					Engine::CurveKey& keyB = channels[2].keys[selection.keyIndex];
					const float movedTime = SnapTime((std::max)(0.0f, keyR.time + dt), state.snapEnabled, state.snapInterval);
					keyR.time = movedTime;
					keyG.time = movedTime;
					keyB.time = movedTime;
					continue;
				}
				if (selection.channelIndex >= channels.size()) {
					continue;
				}
				Engine::CurveChannel& channel = channels[selection.channelIndex];
				if (selection.keyIndex >= channel.keys.size()) {
					continue;
				}
				Engine::CurveKey& key = channel.keys[selection.keyIndex];
				key.time = SnapTime((std::max)(0.0f, key.time + dt), state.snapEnabled, state.snapInterval);
				if (IsColorCurveSet(channels) && selection.channelIndex == 3) {
					key.value = (std::clamp)(key.value + dv, 0.0f, 1.0f);
				} else {
					key.value = ClampKeyValueToVisibleRange(state, key.value + dv);
				}
			}
			state.dragLastMouse = mouse;
			result.valueChanged = true;
			result.anyItemActive = true;
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Marquee && state.marqueeActive) {
			state.marqueeMax = mouse;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				ImVec2 minPos = state.marqueeMin;
				ImVec2 maxPos = state.marqueeMax;
				NormalizeRect(minPos, maxPos);
				if (!io.KeyCtrl) {
					state.ClearSelection();
				}
				for (uint32_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex) {
					if (!state.IsChannelVisible(channelIndex)) {
						continue;
					}
					if (IsColorCurveSet(channels) && (channelIndex == 1 || channelIndex == 2)) {
						continue;
					}
					for (uint32_t keyIndex = 0; keyIndex < channels[channelIndex].keys.size(); ++keyIndex) {
						const Engine::CurveKey& key = channels[channelIndex].keys[keyIndex];
						const ImVec2 keyPos = WorldToScreen(graphRect, state, key.time, key.value);
						if (minPos.x <= keyPos.x && keyPos.x <= maxPos.x && minPos.y <= keyPos.y && keyPos.y <= maxPos.y) {
							state.ToggleSelection((IsColorCurveSet(channels) && channelIndex < 3) ? 0u : channelIndex, keyIndex);
						}
					}
				}
				state.marqueeActive = false;
				state.dragMode = Engine::CurveEditorDragMode::None;
				result.selectionChanged = true;
			}
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Key && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
			if (IsColorCurveSet(channels)) {
				SortColorRgbKeys(channels);
				if (HasAlphaChannel(channels)) {
					channels[3].SortKeys();
				}
			} else {
				for (Engine::CurveChannel& channel : channels) {
					channel.SortKeys();
				}
			}
			state.dragMode = Engine::CurveEditorDragMode::None;
			result.editFinished = true;
		}

		if (state.dragMode == Engine::CurveEditorDragMode::Pan && ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
			state.dragMode = Engine::CurveEditorDragMode::None;
		}
	}
	// 左上のMaxValue入力と、左下のMinValue表示を描画する
	void DrawGraphRangeEditors(const ImRect& topCornerRect, const ImRect& bottomCornerRect,
		Engine::CurveEditorState& state,
		bool& outValueChanged, bool& outEditFinished, bool& outUiBlocking) {

		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 2.0f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(20, 20, 20, 255));
		ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(40, 40, 40, 255));
		ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(20, 20, 20, 255));

		const float topWidth = (std::max)(36.0f, topCornerRect.GetWidth() - 8.0f);
		const ImVec2 topPos(topCornerRect.Min.x + 4.0f, topCornerRect.Min.y + 2.0f);

		ImGui::PushID("CurveGraphTopLeftRangeEditor");
		ImGui::SetCursorScreenPos(topPos);
		ImGui::SetNextItemWidth(topWidth);

		float valueMax = state.visibleValueMax;
		if (ImGui::InputFloat("##VisibleValueMax", &valueMax, 0.0f, 0.0f, "%.1f")) {
			state.visibleValueMax = (std::max)(valueMax, state.visibleValueMin + 0.001f);
			outValueChanged = true;
		}
		outEditFinished |= ImGui::IsItemDeactivatedAfterEdit();
		outUiBlocking |= ImGui::IsItemHovered() || ImGui::IsItemActive();
		ImGui::PopID();

		// 左下ではMinValueを直接編集できるようにする。
		// MaxValue以上にならないようにだけ制限する。
		const float bottomWidth = (std::max)(36.0f, bottomCornerRect.GetWidth() - 8.0f);
		const float textHeight = ImGui::GetFrameHeight();
		const ImVec2 bottomPos(
			bottomCornerRect.Min.x + 4.0f,
			bottomCornerRect.Max.y - textHeight - 2.0f);

		ImGui::PushID("CurveGraphBottomLeftRangeEditor");
		ImGui::SetCursorScreenPos(bottomPos);
		ImGui::SetNextItemWidth(bottomWidth);
		float valueMin = state.visibleValueMin;
		if (ImGui::InputFloat("##VisibleValueMin", &valueMin, 0.0f, 0.0f, "%.1f")) {
			state.visibleValueMin = (std::min)(valueMin, state.visibleValueMax - 0.001f);
			outValueChanged = true;
		}
		outEditFinished |= ImGui::IsItemDeactivatedAfterEdit();
		outUiBlocking |= ImGui::IsItemHovered() || ImGui::IsItemActive();
		ImGui::PopID();

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
	}
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(const char* id, std::span<CurveChannel> channels,
	CurveEditorState& state, const CurveEditSetting& inputSetting) {

	CurveEditResult result{};
	CurveEditSetting setting = inputSetting;

	ImGui::PushID(id);

	// 初回表示時だけ設定値からstateへスナップ設定を取り込む
	if (state.snapInterval <= 0.0f) {
		state.snapEnabled = setting.snap;
		state.snapInterval = setting.snapInterval;
	}
	// 初期表示レンジを明示的に揃える。
	// 旧既定値(-1.0f, 1.0f)や未初期化相当(0.0f, 0.0f)から入っても
	// Min=0.0f, Max=1.0f を基準に開始する。
	if ((state.visibleValueMin == 0.0f && state.visibleValueMax == 0.0f) ||
		(state.visibleValueMin == -1.0f && state.visibleValueMax == 1.0f)) {
		state.visibleValueMin = 0.0f;
		state.visibleValueMax = 1.0f;
	}
	// チャンネル数に合わせて表示状態配列を用意する
	state.EnsureChannelCount(static_cast<uint32_t>(channels.size()));
	std::array<uint32_t, 64> keyCounts{};
	for (uint32_t i = 0; i < channels.size() && i < keyCounts.size(); ++i) {
		keyCounts[i] = static_cast<uint32_t>(channels[i].keys.size());
	}
	// 削除済みキーを指す選択をここで破棄する
	state.RemoveInvalidSelections(static_cast<uint32_t>(channels.size()), keyCounts.data());

	const ImVec2 avail = ImGui::GetContentRegionAvail();
	const ImVec2 editorSize = setting.autoFit ? avail : setting.size;

	ImGui::BeginChild("##CurveEditorRoot", editorSize, true, ImGuiWindowFlags_NoScrollbar);
	const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
	ImGui::SetWindowFontScale(kCurveEditorFontScale);

	// ツールバーはGraph本体より先に描画する
	if (setting.showToolbar) {
		DrawToolbar(channels, state, result);
		ImGui::Separator();
	}

	const float mainAreaHeight = (std::max)(1.0f, ImGui::GetContentRegionAvail().y);
	const float centerHeight = (std::max)(1.0f, mainAreaHeight);
	// 左パネルはチャンネル表示とビュー操作をまとめる
	if (setting.showSidePanels) {
		DrawChannelPanel(channels, state, mainAreaHeight);
		ImGui::SameLine();
	}

	// 右パネル分を差し引いた幅をGraphに使う
	const float sideWidth = setting.showSidePanels ? kCurveInspectorWidth + ImGui::GetStyle().ItemSpacing.x : 0.0f;
	ImGui::BeginGroup();

	const ImVec2 totalCanvasSize(
		(std::max)(160.0f, ImGui::GetContentRegionAvail().x - sideWidth),
		centerHeight);

	const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
	const ImVec2 canvasMax(canvasMin.x + totalCanvasSize.x, canvasMin.y + totalCanvasSize.y);
	const ImRect canvasRect(canvasMin, canvasMax);

	// ルーラー付きレイアウト
	const ImRect topRulerRect(
		ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Min.y),
		ImVec2(canvasRect.Max.x, canvasRect.Min.y + kCurveTopRulerHeight));

	const ImRect leftRulerRect(
		ImVec2(canvasRect.Min.x, canvasRect.Min.y + kCurveTopRulerHeight),
		ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Max.y));

	const ImRect graphRect(
		ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Min.y + kCurveTopRulerHeight),
		canvasRect.Max);
	const ImRect topCornerRect(
		ImVec2(canvasRect.Min.x, canvasRect.Min.y),
		ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Min.y + kCurveTopRulerHeight));
	const ImRect bottomCornerRect(
		ImVec2(canvasRect.Min.x, canvasRect.Max.y - kCurveTopRulerHeight),
		ImVec2(canvasRect.Min.x + kCurveLeftRulerWidth, canvasRect.Max.y));

	if (state.frameSelectionRequest) {
		FitView(graphRect, channels, state);
		state.frameSelectionRequest = false;
	}

	// ここ重要: グラフ入力をImGuiアイテムとして捕まえる
	ImGui::SetCursorScreenPos(graphRect.Min);
	ImGui::InvisibleButton("##CurveGraphInput", graphRect.GetSize(),
		ImGuiButtonFlags_MouseButtonLeft |
		ImGuiButtonFlags_MouseButtonRight |
		ImGuiButtonFlags_MouseButtonMiddle);

	const bool graphActive = ImGui::IsItemActive();
	result.anyItemActive |= graphActive;

	// 描画
	DrawGrid(graphRect, state);
	DrawTimeRuler(topRulerRect, state);
	DrawValueRuler(leftRulerRect, state);

	if (IsColorCurveSet(channels)) {
		DrawColorRgbCurveSamples(graphRect, channels, state);
	}
	for (uint32_t channelIndex = 0; channelIndex < static_cast<uint32_t>(channels.size()); ++channelIndex) {
		if (!state.IsChannelVisible(channelIndex)) {
			continue;
		}
		if (IsColorCurveSet(channels) && channelIndex < 3) {
			continue;
		}
		DrawCurveSamples(graphRect, channels[channelIndex], state);
	}

	DrawKeys(graphRect, channels, state);
	DrawCurrentTimeLine(graphRect, state);
	bool graphOverlayUiBlocking = false;
	const bool valueChangedBeforeCornerEdit = result.valueChanged;
	DrawGraphRangeEditors(topCornerRect, bottomCornerRect, state,
		result.valueChanged, result.editFinished, graphOverlayUiBlocking);
	if (!valueChangedBeforeCornerEdit && result.valueChanged) {
		UpdateVerticalZoomFromRange(graphRect, state);
	}

	// 矩形選択中の半透明領域を描画する
	if (state.marqueeActive) {
		ImVec2 minPos = state.marqueeMin;
		ImVec2 maxPos = state.marqueeMax;
		NormalizeRect(minPos, maxPos);
		ImGui::GetWindowDrawList()->AddRectFilled(minPos, maxPos, IM_COL32(80, 140, 255, 42));
		ImGui::GetWindowDrawList()->AddRect(minPos, maxPos, IM_COL32(100, 165, 255, 180));
	}

	if (!graphOverlayUiBlocking) {
		HandleGraphInput(graphRect, channels, state, result);
	}
	RefreshGridStepsOnly(graphRect, state);

	ImGui::EndGroup();

	// 右側の選択キー詳細
	if (setting.showSidePanels) {
		ImGui::SameLine();
		DrawInspector(channels, state, result, mainAreaHeight);
	}

	ImGui::SetWindowFontScale(previousFontScale);
	ImGui::EndChild();
	ImGui::PopID();
	return result;
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(const char* id, CurveFloat& curve,
	CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(const char* id, CurveVector3& curve,
	CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(const char* id, CurveColor3& curve,
	CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}

Engine::CurveEditResult Engine::MyGUI::CurveEditor(const char* id, CurveColor4& curve,
	CurveEditorState& state, const CurveEditSetting& setting) {

	return CurveEditor(id, GetCurveChannels(curve), state, setting);
}