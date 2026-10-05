#include "CanvasInputEditing.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <array>

//============================================================================
//	CanvasInputEditing internal
//============================================================================

namespace {

	constexpr auto kKeyboardSubmitInputs = std::to_array<KeyDIKCode>({KeyDIKCode::ESCAPE, KeyDIKCode::_1, KeyDIKCode::_2,
		KeyDIKCode::_3, KeyDIKCode::_4, KeyDIKCode::_5, KeyDIKCode::_6, KeyDIKCode::_7, KeyDIKCode::_8, KeyDIKCode::_9,
		KeyDIKCode::_0, KeyDIKCode::MINUS, KeyDIKCode::EQUALS, KeyDIKCode::BACK, KeyDIKCode::TAB, KeyDIKCode::Q, KeyDIKCode::W,
		KeyDIKCode::E, KeyDIKCode::R, KeyDIKCode::T, KeyDIKCode::Y, KeyDIKCode::U, KeyDIKCode::I, KeyDIKCode::O, KeyDIKCode::P,
		KeyDIKCode::LBRACKET, KeyDIKCode::RBRACKET, KeyDIKCode::RETURN, KeyDIKCode::LCONTROL, KeyDIKCode::A, KeyDIKCode::S,
		KeyDIKCode::D, KeyDIKCode::F, KeyDIKCode::G, KeyDIKCode::H, KeyDIKCode::J, KeyDIKCode::K, KeyDIKCode::L,
		KeyDIKCode::SEMICOLON, KeyDIKCode::APOSTROPHE, KeyDIKCode::GRAVE, KeyDIKCode::LSHIFT, KeyDIKCode::BACKSLASH,
		KeyDIKCode::Z, KeyDIKCode::X, KeyDIKCode::C, KeyDIKCode::V, KeyDIKCode::B, KeyDIKCode::N, KeyDIKCode::M,
		KeyDIKCode::COMMA, KeyDIKCode::PERIOD, KeyDIKCode::SLASH, KeyDIKCode::RSHIFT, KeyDIKCode::MULTIPLY, KeyDIKCode::LALT,
		KeyDIKCode::SPACE, KeyDIKCode::CAPITAL, KeyDIKCode::F1, KeyDIKCode::F2, KeyDIKCode::F3, KeyDIKCode::F4, KeyDIKCode::F5,
		KeyDIKCode::F6, KeyDIKCode::F7, KeyDIKCode::F8, KeyDIKCode::F9, KeyDIKCode::F10, KeyDIKCode::F11, KeyDIKCode::F12,
		KeyDIKCode::NUMLOCK, KeyDIKCode::SCROLL, KeyDIKCode::NUMPAD7, KeyDIKCode::NUMPAD8, KeyDIKCode::NUMPAD9,
		KeyDIKCode::SUBTRACT, KeyDIKCode::NUMPAD4, KeyDIKCode::NUMPAD5, KeyDIKCode::NUMPAD6, KeyDIKCode::ADD,
		KeyDIKCode::NUMPAD1, KeyDIKCode::NUMPAD2, KeyDIKCode::NUMPAD3, KeyDIKCode::NUMPAD0, KeyDIKCode::DECIMAL,
		KeyDIKCode::NUMPADENTER, KeyDIKCode::RCONTROL, KeyDIKCode::DIVIDE, KeyDIKCode::RALT, KeyDIKCode::HOME, KeyDIKCode::UP,
		KeyDIKCode::PRIOR, KeyDIKCode::LEFT, KeyDIKCode::RIGHT, KeyDIKCode::END, KeyDIKCode::DOWN, KeyDIKCode::NEXT,
		KeyDIKCode::INSERT, KeyDIKCode::DELETE_KEY, KeyDIKCode::LWIN, KeyDIKCode::RWIN, KeyDIKCode::APPS});

	constexpr auto kGamepadSubmitInputs = std::to_array<GamePadButtons>(
		{GamePadButtons::ARROW_UP, GamePadButtons::ARROW_DOWN, GamePadButtons::ARROW_LEFT, GamePadButtons::ARROW_RIGHT,
			GamePadButtons::START, GamePadButtons::BACK, GamePadButtons::LEFT_THUMB, GamePadButtons::RIGHT_THUMB,
			GamePadButtons::LEFT_SHOULDER, GamePadButtons::RIGHT_SHOULDER, GamePadButtons::LEFT_TRIGGER,
			GamePadButtons::RIGHT_TRIGGER, GamePadButtons::A, GamePadButtons::B, GamePadButtons::X, GamePadButtons::Y});

	const char* KeyboardInputName(KeyDIKCode input) {

		switch (input) {
		case KeyDIKCode::NUMPADENTER:
			return "NUMPADENTER";
		case KeyDIKCode::RCONTROL:
			return "RCONTROL";
		case KeyDIKCode::DIVIDE:
			return "DIVIDE";
		case KeyDIKCode::RALT:
			return "RALT";
		case KeyDIKCode::HOME:
			return "HOME";
		case KeyDIKCode::UP:
			return "UP";
		case KeyDIKCode::PRIOR:
			return "PAGE_UP";
		case KeyDIKCode::LEFT:
			return "LEFT";
		case KeyDIKCode::RIGHT:
			return "RIGHT";
		case KeyDIKCode::END:
			return "END";
		case KeyDIKCode::DOWN:
			return "DOWN";
		case KeyDIKCode::NEXT:
			return "PAGE_DOWN";
		case KeyDIKCode::INSERT:
			return "INSERT";
		case KeyDIKCode::DELETE_KEY:
			return "DELETE";
		case KeyDIKCode::LWIN:
			return "LWIN";
		case KeyDIKCode::RWIN:
			return "RWIN";
		case KeyDIKCode::APPS:
			return "APPS";
		default:
			return Engine::EnumAdapter<KeyDIKCode>::ToString(input);
		}
	}

	bool DrawKeyboardInputCombo(const char* label, KeyDIKCode& current) {

		bool changed = false;
		if (ImGui::BeginCombo(label, KeyboardInputName(current))) {
			for (KeyDIKCode input : kKeyboardSubmitInputs) {

				const bool selected = current == input;
				if (ImGui::Selectable(KeyboardInputName(input), selected)) {
					current = input;
					changed = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	bool DrawGamepadInputCombo(const char* label, GamePadButtons& current) {

		bool changed = false;
		if (ImGui::BeginCombo(label, Engine::EnumAdapter<GamePadButtons>::ToString(current))) {
			for (GamePadButtons input : kGamepadSubmitInputs) {

				const bool selected = current == input;
				if (ImGui::Selectable(Engine::EnumAdapter<GamePadButtons>::ToString(input), selected)) {
					current = input;
					changed = true;
				}
				if (selected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
		return changed;
	}

	template <typename T, size_t Size, typename DrawComboFn>
	Engine::ValueEditResult DrawInputBindings(const char* label, std::vector<T>& bindings,
		const std::array<T, Size>& candidates, T preferred, DrawComboFn&& drawCombo) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		int32_t removeIndex = -1;
		for (int32_t i = 0; i < static_cast<int32_t>(bindings.size()); ++i) {

			ImGui::PushID(i);
			const float removeWidth = ImGui::CalcTextSize("削除").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetNextItemWidth(
				(std::max)(ImGui::GetContentRegionAvail().x - removeWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f));
			if (drawCombo("##Input", bindings[static_cast<size_t>(i)])) {
				result.valueChanged = true;
				result.editFinished = true;
			}
			result.anyItemActive |= ImGui::IsItemActive();
			ImGui::SameLine();
			if (ImGui::SmallButton("削除")) {
				removeIndex = i;
			}
			ImGui::PopID();
		}
		if (0 <= removeIndex) {
			bindings.erase(bindings.begin() + removeIndex);
			result.valueChanged = true;
			result.editFinished = true;
		}

		auto next = std::find(candidates.begin(), candidates.end(), preferred);
		if (next == candidates.end() || std::find(bindings.begin(), bindings.end(), *next) != bindings.end()) {
			next = std::find_if(candidates.begin(), candidates.end(),
				[&](T candidate) { return std::find(bindings.begin(), bindings.end(), candidate) == bindings.end(); });
		}
		if (next == candidates.end()) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("追加", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)) && next != candidates.end()) {
			bindings.emplace_back(*next);
			result.valueChanged = true;
			result.editFinished = true;
		}
		if (next == candidates.end()) {
			ImGui::EndDisabled();
		}

		if (result.valueChanged) {
			std::vector<T> unique;
			unique.reserve(bindings.size());
			for (T binding : bindings) {
				if (std::find(unique.begin(), unique.end(), binding) == unique.end()) {
					unique.emplace_back(binding);
				}
			}
			bindings = std::move(unique);
		}
		Engine::MyGUI::EndPropertyRow();
		return result;
	}

}

//============================================================================
//	CanvasInputEditing functions
//============================================================================

Engine::ValueEditResult Engine::CanvasInputEditing::DrawKeyboardBindings(
	const char* label, std::vector<KeyDIKCode>& bindings, KeyDIKCode preferred) {

	// 入力候補から重複のないキーを選ぶ
	return DrawInputBindings(label, bindings, kKeyboardSubmitInputs, preferred, DrawKeyboardInputCombo);
}

Engine::ValueEditResult Engine::CanvasInputEditing::DrawGamepadBindings(
	const char* label, std::vector<GamePadButtons>& bindings, GamePadButtons preferred) {

	// 入力候補から重複のないボタンを選ぶ
	return DrawInputBindings(label, bindings, kGamepadSubmitInputs, preferred, DrawGamepadInputCombo);
}
