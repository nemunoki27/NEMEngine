#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

#include <string>
#include <imgui.h>

namespace Engine::ImGuiUtility {

	// 列挙値を選択する
	template <typename T>
	bool EnumCombo(const char* label, T* current) noexcept {

		int idx = static_cast<int>(EnumAdapter<T>::GetIndex(*current));
		bool changed = ImGui::Combo(label, &idx, EnumAdapter<T>::GetEnumArray().data(),
			static_cast<int>(EnumAdapter<T>::GetEnumCount()));
		if (changed) {

			*current = EnumAdapter<T>::GetValue(static_cast<std::uint32_t>(idx));
		}
		return changed;
	}
}

namespace Easing {

	// 補間方式を選択する
	void SelectEasingType(EasingType& easingType, const std::string& label = "label", float itemWidth = 200.0f);
}
