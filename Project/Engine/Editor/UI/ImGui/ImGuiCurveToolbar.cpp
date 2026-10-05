#include "ImGuiCurveEditorInternal.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>

namespace Engine::CurveEditorUtility {

	// Toolbarの入力高さを揃える
	void PushToolbarItemStyle() {

		const float fontSize = ImGui::GetFontSize();
		const float paddingY = (std::max)(0.0f, (kCurveToolbarItemSize.y - fontSize) * 0.5f);
		ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(ImGui::GetStyle().FramePadding.x, paddingY));
	}

	// Toolbarのボタンを表示する
	bool DrawToolbarButton(const char* label) {

		PushToolbarItemStyle();
		ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.5f, 0.5f));
		const bool clicked = ImGui::Button(label, kCurveToolbarItemSize);
		ImGui::PopStyleVar(2);
		return clicked;
	}

	// Toolbarの数値入力を表示する
	bool DrawToolbarDragFloat(
		const char* label, float& value, float speed, float minValue, float maxValue, const char* format) {

		PushToolbarItemStyle();
		ImGui::SetNextItemWidth(kCurveToolbarItemSize.x);
		const bool changed = ImGui::DragFloat(label, &value, speed, minValue, maxValue, format);
		ImGui::PopStyleVar();
		return changed;
	}

	// チャンネルの表示切替を描く
	bool DrawToolbarCheckbox(const char* label, bool& value, const ImVec4& labelColor) {

		ImGui::PushID(label);

		ImGui::TextColored(labelColor, "%s", label);
		ImGui::SameLine(0.0f, 3.0f);

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
			const ImVec2 checkPos(squareMin.x + (kCurveToolbarItemSize.y - checkSize) * 0.5f,
				squareMin.y + (kCurveToolbarItemSize.y - checkSize) * 0.5f);
			ImGui::RenderCheckMark(drawList, checkPos, ImGui::GetColorU32(ImGuiCol_CheckMark), checkSize);
		}

		ImGui::PopID();
		return clicked;
	}

	// チャンネル別の表示を切り替える
	void DrawToolbarChannelVisibility(std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state) {

		if (channels.empty()) {
			return;
		}

		ImGui::SameLine();

		if (IsColorChannelSet(channels)) {
			bool rgbVisible = state.IsChannelVisible(0);
			if (DrawToolbarCheckbox("RGB", rgbVisible, ImVec4(1.0f, 1.0f, 1.0f, 1.0f))) {
				state.SetChannelVisible(0, rgbVisible);
				state.SetChannelVisible(1, rgbVisible);
				state.SetChannelVisible(2, rgbVisible);
			}
			if (channels.size() >= 4) {
				ImGui::SameLine(0.0f, 8.0f);
				bool alphaVisible = state.IsChannelVisible(3);
				const Engine::CurveChannel& alphaChannel = channels[3];
				if (DrawToolbarCheckbox("A", alphaVisible,
						ImVec4(alphaChannel.displayColor.r, alphaChannel.displayColor.g, alphaChannel.displayColor.b,
							alphaChannel.displayColor.a))) {
					state.SetChannelVisible(3, alphaVisible);
				}
			}
			return;
		}

		if (IsQuaternionCurveSet(channels)) {
			for (uint32_t i = 0; i < channels.size(); ++i) {
				if (i != 0) {
					ImGui::SameLine(0.0f, 8.0f);
				}
				const Engine::CurveChannel& channel = channels[i];
				bool visible = state.IsChannelVisible(i);
				if (DrawToolbarCheckbox(channel.name.c_str(), visible,
						ImVec4(
							channel.displayColor.r, channel.displayColor.g, channel.displayColor.b, channel.displayColor.a))) {
					state.SetChannelVisible(i, visible);
				}
			}
			return;
		}

		for (uint32_t i = 0; i < channels.size(); ++i) {
			if (i != 0) {
				ImGui::SameLine(0.0f, 8.0f);
			}
			const Engine::CurveChannel& channel = channels[i];
			bool visible = state.IsChannelVisible(i);
			if (DrawToolbarCheckbox(channel.name.c_str(), visible,
					ImVec4(channel.displayColor.r, channel.displayColor.g, channel.displayColor.b, channel.displayColor.a))) {
				state.SetChannelVisible(i, visible);
			}
		}
	}

	// 時刻と表示操作を表示する
	void DrawToolbar(
		std::span<Engine::CurveChannel> channels, Engine::CurveEditorState& state, Engine::CurveEditResult& result) {

		const float previousFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
		ImGui::SetWindowFontScale(kCurveEditorFontScale);

		if (DrawToolbarButton("全体表示")) {
			state.frameSelectionRequest = true;
			result.valueChanged = true;
		}
		ImGui::SameLine();

		// 時間のスナップ間隔を固定する
		state.snapEnabled = true;
		state.snapInterval = 0.001f;
		DrawToolbarDragFloat("時間", state.currentTime, 0.01f, 0.0f, 10000.0f, "%.3f");
		DrawToolbarChannelVisibility(channels, state);

		ImGui::SetWindowFontScale(previousFontScale);
	}

	// 表示する値の上下限を編集する
	void DrawGraphRangeEditors(const ImRect& topCornerRect, const ImRect& bottomCornerRect, Engine::CurveEditorState& state,
		bool& outValueChanged, bool& outEditFinished, bool& outUIBlocking) {

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
		outUIBlocking |= ImGui::IsItemHovered() || ImGui::IsItemActive();
		ImGui::PopID();

		// 下限値を上限値の範囲内で編集する
		const float bottomWidth = (std::max)(36.0f, bottomCornerRect.GetWidth() - 8.0f);
		const float textHeight = ImGui::GetFrameHeight();
		const ImVec2 bottomPos(bottomCornerRect.Min.x + 4.0f, bottomCornerRect.Max.y - textHeight - 2.0f);

		ImGui::PushID("CurveGraphBottomLeftRangeEditor");
		ImGui::SetCursorScreenPos(bottomPos);
		ImGui::SetNextItemWidth(bottomWidth);
		float valueMin = state.visibleValueMin;
		if (ImGui::InputFloat("##VisibleValueMin", &valueMin, 0.0f, 0.0f, "%.1f")) {
			state.visibleValueMin = (std::min)(valueMin, state.visibleValueMax - 0.001f);
			outValueChanged = true;
		}
		outEditFinished |= ImGui::IsItemDeactivatedAfterEdit();
		outUIBlocking |= ImGui::IsItemHovered() || ImGui::IsItemActive();
		ImGui::PopID();

		ImGui::PopStyleColor(3);
		ImGui::PopStyleVar();
	}
} // Engine::CurveEditorUtility
