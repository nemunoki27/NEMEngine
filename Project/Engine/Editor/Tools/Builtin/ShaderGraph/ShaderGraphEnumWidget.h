#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <string_view>

// imgui
#include <imgui.h>

namespace Engine::ShaderGraphWidgets {

	// DirectXの列挙名から型の接頭辞を除いて表示する
	template <typename T>
	Engine::ValueEditResult D3D12EnumCombo(const char* label, T& currentValue) {

		Engine::ValueEditResult result{};
		if (!Engine::MyGUI::BeginPropertyRow(label)) {
			return result;
		}

		const float width = ImGui::GetContentRegionAvail().x;
		ImGui::SetNextItemWidth(width <= 1.0f ? 1.0f : width);
		int32_t currentIndex = static_cast<int32_t>(Engine::EnumAdapter<T>::GetIndex(currentValue));
		const auto itemGetter = [](void*, int32_t index) -> const char* {
			constexpr auto names = magic_enum::enum_names<T>();
			constexpr std::string_view typeName = magic_enum::enum_type_name<T>();
			constexpr size_t separator = typeName.rfind('_');
			constexpr std::string_view prefix =
				separator == std::string_view::npos ? std::string_view{} : typeName.substr(0, separator + 1);
			if (index < 0 || names.size() <= static_cast<size_t>(index)) {
				return "";
			}
			std::string_view name = names[static_cast<size_t>(index)];
			if (!prefix.empty() && name.starts_with(prefix)) {
				name.remove_prefix(prefix.size());
			}
			return name.data();
		};
		result.valueChanged = ImGui::Combo(
			"##Value", &currentIndex, itemGetter, nullptr, static_cast<int32_t>(Engine::EnumAdapter<T>::GetEnumCount()));
		if (result.valueChanged) {
			currentValue = Engine::EnumAdapter<T>::GetValue(static_cast<uint32_t>(currentIndex));
		}
		result.anyItemActive = ImGui::IsItemActive();
		result.editFinished = result.valueChanged || ImGui::IsItemDeactivatedAfterEdit();
		Engine::MyGUI::EndPropertyRow();
		return result;
	}
} // Engine::ShaderGraphWidgets
