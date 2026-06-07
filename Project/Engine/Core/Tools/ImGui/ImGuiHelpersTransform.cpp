#include "ImGuiHelpersInternal.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <format>

namespace {

	// Engine::AxisをDrawDragFieldsで使う軸文字へ変換する
	char ToAxisChar(Engine::Axis axis) {
		switch (axis) {
		case Engine::Axis::X: return 'X';
		case Engine::Axis::Y: return 'Y';
		case Engine::Axis::Z: return 'Z';
		default: break;
		}
		return '\0';
	}

} // namespace

// 4x4行列を読み取り専用のテーブル形式で表示する
void Engine::MyGUI::TextMatrix4x4(const char* label, const Matrix4x4& value, uint32_t precision) {
	if (!BeginPropertyRow(label)) { return; }
	const std::string tableID = std::string("##MyGUI_Matrix_") + label;
	// 4x4の格子状に値を配置し、行列全体の構成を視覚的に把握しやすくする
	if (ImGui::BeginTable(tableID.c_str(), 4, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
		for (int row = 0; row < 4; ++row) {
			ImGui::TableNextRow();
			for (int col = 0; col < 4; ++col) {
				ImGui::TableSetColumnIndex(col);
				const std::string text = FormatFloat(value.m[row][col], precision);
				const std::string buttonID = std::format("##{}_{}_{}", label, row, col);
				// 数値をボタンとして描画し、クリックで値をクリップボードにコピー可能にする
				if (ImGui::Button((text + buttonID).c_str(), ImVec2(-FLT_MIN, 0.0f))) { ImGui::SetClipboardText(text.c_str()); }
				if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Click to copy"); }
			}
		}
		ImGui::EndTable();
	}
	EndPropertyRow();
}

Engine::ValueEditResult Engine::MyGUI::DragFloat(const char* label, float& value, const FloatEditSetting& setting) {
	float values[1] = { value };
	const char axis = setting.floatAxis.has_value() ? ToAxisChar(setting.floatAxis.value()) : '\0';
	ValueEditResult result = DrawDragFields(label, { axis, '\0', '\0', '\0' }, values, 1, setting);
	if (result.valueChanged) { value = values[0]; }
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragVector2(const char* label, Vector2& value, const FloatEditSetting& setting) {
	float values[2] = { value.x, value.y };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', '\0', '\0' }, values, 2, setting);
	if (result.valueChanged) { value.x = values[0]; value.y = values[1]; }
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragVector3(const char* label, Vector3& value, const FloatEditSetting& setting) {
	float values[3] = { value.x, value.y, value.z };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', 'Z', '\0' }, values, 3, setting);
	if (result.valueChanged) { value.x = values[0]; value.y = values[1]; value.z = values[2]; }
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragVector4(const char* label, Vector4& value, const FloatEditSetting& setting) {
	float values[4] = { value.x, value.y, value.z, value.w };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', 'Z', 'W' }, values, 4, setting);
	if (result.valueChanged) { value.x = values[0]; value.y = values[1]; value.z = values[2]; value.w = values[3]; }
	return result;
}

Engine::ValueEditResult Engine::MyGUI::DragQuaternion(const char* label, Quaternion& value, bool displayEuler, const FloatEditSetting& setting) {
	float values[4] = { value.x, value.y, value.z, value.w };
	ValueEditResult result = DrawDragFields(label, { 'X', 'Y', 'Z', 'W' }, values, 4, setting);
	// クォータニオンの各成分を直接編集した後は、計算誤差等による歪みを防ぐため常に正規化する
	if (result.valueChanged) { value.x = values[0]; value.y = values[1]; value.z = values[2]; value.w = values[3]; value = Quaternion::Normalize(value); }
	if (displayEuler) {
		// クォータニオンは直感的な理解が難しいため、補助情報としてオイラー角を表示
		ImGui::Separator();
		Vector3 euler = Quaternion::ToEulerAngles(value);
		const float eulerValues[3] = { euler.x, euler.y, euler.z };
		DrawTextFields((std::string(label) + " (Euler Deg)").c_str(), { 'X', 'Y', 'Z', '\0' }, eulerValues, 3, 3);
	}
	return result;
}
