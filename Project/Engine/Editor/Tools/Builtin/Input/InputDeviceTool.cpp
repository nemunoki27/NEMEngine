#include "InputDeviceTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <array>

#include <magic_enum.hpp>
#include <imgui.h>

//============================================================================
//	InputDeviceTool helpers
//============================================================================
namespace {

	// 列挙の名前一覧でコンボを描き、選択値を対応するenum値へ反映する
	template <typename T>
	bool DrawEnumCombo(const char* label, int32_t& code) {

		const std::array<const char*, magic_enum::enum_count<T>()> names = Engine::EnumAdapter<T>::GetEnumArray();
		int current = 0;
		for (size_t i = 0; i < names.size(); ++i) {
			if (static_cast<int32_t>(magic_enum::enum_value<T>(i)) == code) {
				current = static_cast<int>(i);
				break;
			}
		}
		if (ImGui::Combo(label, &current, names.data(), static_cast<int>(names.size()))) {
			code = static_cast<int32_t>(magic_enum::enum_value<T>(current));
			return true;
		}
		return false;
	}

}

//============================================================================
//	InputDeviceTool classMethods
//============================================================================

void Engine::InputDeviceTool::Tick([[maybe_unused]] ToolContext& context) {

	// 入力更新自体はEngineFrameworkで毎フレーム行うためここでは何もしない
}

void Engine::InputDeviceTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::InputDeviceTool::DrawEditorTool([[maybe_unused]] const EditorToolContext& context) {

	if (openWindow_) {
		DrawWindow();
	}
}

void Engine::InputDeviceTool::DrawWindow() {

	Input* input = Input::GetInstance();
	if (!input) {
		return;
	}

	if (ImGui::Begin("入力デバイス", &openWindow_)) {

		// 設定の保存と読込
		if (ImGui::Button("保存")) { input->SaveConfig(); }
		ImGui::SameLine();
		if (ImGui::Button("読込")) { input->LoadConfig(); }

		ImGui::SeparatorText("入力タイプ");
		ImGui::Text("現在の入力タイプ: %s",
			input->GetType() == InputType::GamePad ? "ゲームパッド" : "マウス");

		ImGui::SeparatorText("ゲームパッド");
		float deadZone = input->GetDeadZone();
		if (ImGui::DragFloat("デッドゾーン", &deadZone, 100.0f, 0.0f, 32767.0f)) {
			input->SetDeadZone(deadZone);
		}

		ImGui::SeparatorText("マウス範囲制御");
		bool rangeControl = input->GetMouseRangeControl();
		if (ImGui::Checkbox("範囲制御を有効", &rangeControl)) {
			input->SetMouseRangeControl(rangeControl);
		}
		Vector2 areaPos = input->GetMouseAreaPos();
		Vector2 areaSize = input->GetMouseAreaSize();
		bool areaEdit = false;
		areaEdit |= ImGui::DragFloat2("範囲中心(クライアント座標)", &areaPos.x, 1.0f);
		areaEdit |= ImGui::DragFloat2("範囲サイズ", &areaSize.x, 1.0f);
		if (areaEdit) {
			input->SetMouseArea(areaPos, areaSize);
		}
		// 解除ショートカット(DIKコード)
		int modKey = input->GetMouseReleaseModKey();
		int triggerKey = input->GetMouseReleaseTriggerKey();
		bool keyEdit = false;
		keyEdit |= DrawEnumCombo<KeyDIKCode>("解除Mod", modKey);
		keyEdit |= DrawEnumCombo<KeyDIKCode>("解除Trigger", triggerKey);
		if (keyEdit) {
			input->SetMouseReleaseShortcut(modKey, triggerKey);
		}
		ImGui::TextDisabled("解除はMod押下+Triggerキー");
	}
	ImGui::End();
}
