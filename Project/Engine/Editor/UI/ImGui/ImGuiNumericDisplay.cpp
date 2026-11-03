#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <format>

namespace Engine {

	// 各成分を同じ幅で表示する
	void DrawTextFields(const char* label, const std::array<char, 4>& axes,
		const float* values, uint32_t count, uint32_t precision) {

		if (!Engine::MyGUI::BeginPropertyRow(label)) return;
		const float width = Engine::MyGUI::CalcEqualItemWidth(count);
		for (uint32_t i = 0; i < count; ++i) {
			if (i > 0) ImGui::SameLine();
			ImGui::PushID(static_cast<int>(i));
			ImGui::BeginGroup();
			const Engine::AxisDisplayInfo axis = Engine::GetAxisDisplayInfo(axes[i], Engine::AxisLabelPalette::Copyable);
			ImGui::PushStyleColor(ImGuiCol_Text, axis.color);
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted(axis.name);
			ImGui::PopStyleColor();
			ImGui::SameLine(0.0f, 6.0f);
			Engine::DrawCopyableValue("##Value", Engine::FormatFloat(values[i], precision), (std::max)(1.0f, width - 20.0f));
			ImGui::EndGroup();
			ImGui::PopID();
		}
		Engine::MyGUI::EndPropertyRow();
	}
}

void Engine::DrawCopyableValue(const char* id, const std::string& value, float width) {

	const ImVec2 padding = ImGui::GetStyle().FramePadding;
	width = (std::max)(1.0f, width);
	const float textWidth = (std::max)(1.0f, width - padding.x * 2.0f);
	const ImVec2 textSize = ImGui::CalcTextSize(value.data(), value.data() + value.size(), false, textWidth);
	const ImVec2 size(width, (std::max)(ImGui::GetFrameHeight(), textSize.y + padding.y * 2.0f));
	const ImVec2 position = ImGui::GetCursorScreenPos();
	if (ImGui::Button(id, size)) ImGui::SetClipboardText(value.c_str());
	// 表示文字列をIDへ含めず、値の変化中もクリック先を保持する
	ImGui::GetWindowDrawList()->AddText(nullptr, 0.0f, ImVec2(position.x + padding.x, position.y + padding.y),
		ImGui::GetColorU32(ImGuiCol_Text), value.data(), value.data() + value.size(), textWidth);
	if (ImGui::BeginPopupContextItem()) {
		if (ImGui::MenuItem("コピー")) ImGui::SetClipboardText(value.c_str());
		ImGui::EndPopup();
	}
}

void Engine::MyGUI::Text(const char* text) {

	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
	ImGui::TextUnformatted(text);
	ImGui::PopTextWrapPos();
}

void Engine::MyGUI::Text(const char* label, const std::string& value) {

	if (!BeginPropertyRow(label)) return;
	DrawCopyableValue("##Value", value, ImGui::GetContentRegionAvail().x);
	EndPropertyRow();
}

void Engine::MyGUI::TextInt(const char* label, int32_t value) {

	TextInt64(label, value);
}

void Engine::MyGUI::TextUInt(const char* label, uint32_t value) {

	TextUInt64(label, value);
}

void Engine::MyGUI::TextInt64(const char* label, int64_t value) {

	Text(label, std::to_string(value));
}

void Engine::MyGUI::TextUInt64(const char* label, uint64_t value) {

	Text(label, std::to_string(value));
}

void Engine::MyGUI::TextFloat(const char* label, float value, uint32_t precision) {

	Text(label, FormatFloat(value, precision));
}

void Engine::MyGUI::TextDouble(const char* label, double value, uint32_t precision) {

	Text(label, std::format("{:.{}f}", value, precision));
}

void Engine::MyGUI::TextBool(const char* label, bool value) {

	Text(label, value ? "true" : "false");
}

void Engine::MyGUI::TextVector2(const char* label, const Vector2& value, uint32_t precision) {

	const float values[2] = {value.x, value.y};
	DrawTextFields(label, {'X', 'Y', '\0', '\0'}, values, 2, precision);
}

void Engine::MyGUI::TextVector3(const char* label, const Vector3& value, uint32_t precision) {

	const float values[3] = {value.x, value.y, value.z};
	DrawTextFields(label, {'X', 'Y', 'Z', '\0'}, values, 3, precision);
}

void Engine::MyGUI::TextVector4(const char* label, const Vector4& value, uint32_t precision) {

	const float values[4] = {value.x, value.y, value.z, value.w};
	DrawTextFields(label, {'X', 'Y', 'Z', 'W'}, values, 4, precision);
}

void Engine::MyGUI::TextQuaternion(const char* label, const Quaternion& value, uint32_t precision) {

	const float values[4] = {value.x, value.y, value.z, value.w};
	DrawTextFields(label, {'X', 'Y', 'Z', 'W'}, values, 4, precision);
}

void Engine::MyGUI::TextColor3(const char* label, const Color3& value, uint32_t precision) {

	const float values[3] = {value.r, value.g, value.b};
	DrawTextFields(label, {'R', 'G', 'B', '\0'}, values, 3, precision);
}

void Engine::MyGUI::TextColor4(const char* label, const Color4& value, uint32_t precision) {

	const float values[4] = {value.r, value.g, value.b, value.a};
	DrawTextFields(label, {'R', 'G', 'B', 'A'}, values, 4, precision);
}
