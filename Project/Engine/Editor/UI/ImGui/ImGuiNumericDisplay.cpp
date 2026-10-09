#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <format>

namespace {

	constexpr float kAxisLabelWidth = 14.0f;

	//============================================================================
	//	レイアウトヘルパー
	//============================================================================
	// フィールドの幅を計算する
	float CalcFieldWidth(int fieldCount) {

		const float avail = (std::max)(1.0f, ImGui::GetContentRegionAvail().x);
		const float spacing = ImGui::GetStyle().ItemSpacing.x;
		const float fieldArea = (std::max)(1.0f, avail - spacing * static_cast<float>((std::max)(0, fieldCount - 1)));
		return fieldArea / static_cast<float>(fieldCount);
	}

	//============================================================================
	//	描画ヘルパー
	//============================================================================
	// 軸ラベルを描画する
	void DrawAxisLabel(char axis) {

		const Engine::AxisDisplayInfo info = Engine::GetAxisDisplayInfo(axis, Engine::AxisLabelPalette::Copyable);
		ImGui::PushStyleColor(ImGuiCol_Text, info.color);
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(info.name);
		ImGui::PopStyleColor();
	}
	// コピー可能な値ボックスを描画する
	void DrawCopyableValueBox(const char* id, char axis, const std::string& valueText, float width) {

		ImGui::PushID(id);

		ImGui::BeginGroup();

		// 軸ラベル
		DrawAxisLabel(axis);
		ImGui::SameLine(0.0f, 6.0f);

		const ImVec2 buttonSize(width - kAxisLabelWidth - 6.0f, ImGui::GetFrameHeight());
		if (ImGui::Button(valueText.c_str(), buttonSize)) {
			ImGui::SetClipboardText(valueText.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("クリックでコピー");
		}
		ImGui::EndGroup();
		ImGui::PopID();
	}
	// 複数のコピー可能な値フィールドを描画する
	void DrawCopyableTextFields(
		const char* label, const std::array<char, 4>& axes, const float* values, int count, uint32_t precision) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return;
		}

		const float fieldWidth = CalcFieldWidth(count);
		for (int i = 0; i < count; ++i) {

			if (i > 0) {
				ImGui::SameLine();
			}
			const std::string text = Engine::FormatFloat(values[i], precision);
			const std::string id = std::format("{}_{}", label, i);
			DrawCopyableValueBox(id.c_str(), axes[i], text, fieldWidth);
		}
		Engine::MyGUI::EndPropertyRow();
	}
	// 単一のコピー可能な値フィールドを描画する
	void DrawScalarTextField(const char* label, float value, uint32_t precision) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return;
		}

		const std::string text = Engine::FormatFloat(value, precision);
		if (ImGui::Button(text.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {
			ImGui::SetClipboardText(text.c_str());
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Click to copy");
		}
		Engine::MyGUI::EndPropertyRow();
	}

}

void Engine::MyGUI::Text(const char* label, const std::string& value) {

	if (!BeginPropertyRow(label)) {
		return;
	}
	// 長いパスも欄内で折り返す
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
	ImGui::TextUnformatted(value.c_str());
	ImGui::PopTextWrapPos();
	if (ImGui::BeginPopupContextItem("##TextContextMenu")) {
		if (ImGui::MenuItem("コピー")) {
			ImGui::SetClipboardText(value.c_str());
		}
		ImGui::EndPopup();
	}
	EndPropertyRow();
}

void Engine::MyGUI::TextFloat(const char* label, float value, uint32_t precision) {

	DrawScalarTextField(label, value, precision);
}

void Engine::MyGUI::TextVector2(const char* label, const Vector2& value, uint32_t precision) {

	const float values[2] = {value.x, value.y};
	DrawCopyableTextFields(label, {'X', 'Y', '\0', '\0'}, values, 2, precision);
}

void Engine::MyGUI::TextVector3(const char* label, const Vector3& value, uint32_t precision) {

	const float values[3] = {value.x, value.y, value.z};
	DrawCopyableTextFields(label, {'X', 'Y', 'Z', '\0'}, values, 3, precision);
}

void Engine::MyGUI::TextQuaternion(const char* label, const Quaternion& value, uint32_t precision) {

	const float values[4] = {value.x, value.y, value.z, value.w};
	DrawCopyableTextFields(label, {'X', 'Y', 'Z', 'W'}, values, 4, precision);
}
