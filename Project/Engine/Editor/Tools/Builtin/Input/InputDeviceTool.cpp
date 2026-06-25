#include "InputDeviceTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Platform/Input/InputSystem.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>

// magic_enum
#include <magic_enum.hpp>
// imgui
#include <imgui.h>
// c++
#include <algorithm>
#include <string>
#include <array>

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

	// デバイス種別に応じてコードを列挙名で表示する、未知値は数値で出す
	std::string CodeName(InputType device, int32_t code) {

		const std::string_view name = (device == InputType::GamePad)
			? magic_enum::enum_name(static_cast<GamePadButtons>(code))
			: magic_enum::enum_name(static_cast<KeyDIKCode>(code));
		return name.empty() ? std::to_string(code) : std::string(name);
	}

	// デバイス順、判定種別、コードで安定ソートして重複を除く
	void SortAndDedup(std::vector<InputDetectTrigger>& triggers) {

		std::sort(triggers.begin(), triggers.end(),
			[](const InputDetectTrigger& a, const InputDetectTrigger& b) {
				if (a.device != b.device) { return static_cast<int>(a.device) < static_cast<int>(b.device); }
				if (a.isMovement != b.isMovement) { return a.isMovement < b.isMovement; }
				return a.code < b.code;
			});
		triggers.erase(std::unique(triggers.begin(), triggers.end(),
			[](const InputDetectTrigger& a, const InputDetectTrigger& b) {
				return a.device == b.device && a.isMovement == b.isMovement && a.code == b.code;
			}), triggers.end());
	}
}

//============================================================================
//	InputDeviceTool classMethods
//============================================================================

void Engine::InputDeviceTool::Tick(ToolContext& context) {

	// 入力更新自体はEngineFrameworkで毎フレーム行うためここでは何もしない
	(void)context;
}

void Engine::InputDeviceTool::OpenEditorTool() {

	openWindow_ = true;
}

void Engine::InputDeviceTool::DrawEditorTool(const EditorToolContext& context) {

	(void)context;
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
			EnumAdapter<InputType>::ToString(input->GetType()));

		bool autoUpdate = input->GetAutoUpdateInputType();
		if (ImGui::Checkbox("入力タイプを自動で更新", &autoUpdate)) {
			input->SetAutoUpdateInputType(autoUpdate);
		}
		float threshold = input->GetMovementThreshold();
		if (ImGui::DragFloat("移動判定しきい値", &threshold, 0.01f, 0.0f, 1.0f)) {
			input->SetMovementThreshold(threshold);
		}

		std::vector<InputDetectTrigger>& triggers = input->DetectTriggers();
		const char* deviceItems[] = { "Keyboard", "GamePad" };

		// 検知入力の追加を一番上に置く
		ImGui::SeparatorText("検知入力の追加");
		ImGui::SetNextItemWidth(110.0f);
		ImGui::Combo("デバイス##draft", &draftDevice_, deviceItems, 2);
		ImGui::SameLine();
		ImGui::Checkbox("移動で判定##draft", &draftIsMovement_);
		// ボタン/キー判定のときだけ列挙コンボでコードを選ぶ
		if (!draftIsMovement_) {
			ImGui::SetNextItemWidth(180.0f);
			if (draftDevice_ == static_cast<int>(InputType::GamePad)) {
				DrawEnumCombo<GamePadButtons>("入力##draft", draftCode_);
			} else {
				DrawEnumCombo<KeyDIKCode>("入力##draft", draftCode_);
			}
		}
		if (ImGui::Button("追加")) {
			triggers.push_back({ static_cast<InputType>(draftDevice_), draftIsMovement_, draftCode_ });
			SortAndDedup(triggers);
		}

		// 一覧は折りたたみヘッダに入れて縦に広がらないようにする
		if (MyGUI::CollapsingHeader("検知入力一覧", false)) {

			int32_t removeIndex = -1;
			for (int32_t i = 0; i < static_cast<int32_t>(triggers.size()); ++i) {

				const InputDetectTrigger& trigger = triggers[i];
				ImGui::PushID(i);
				ImGui::BulletText("%s : %s", deviceItems[static_cast<int>(trigger.device)],
					trigger.isMovement ? "移動" : CodeName(trigger.device, trigger.code).c_str());
				ImGui::SameLine();
				if (ImGui::SmallButton("削除")) { removeIndex = i; }
				ImGui::PopID();
			}
			if (removeIndex >= 0) {
				triggers.erase(triggers.begin() + removeIndex);
				SortAndDedup(triggers);
			}
		}

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
