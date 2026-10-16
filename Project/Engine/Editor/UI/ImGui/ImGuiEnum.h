#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

#include <string>
#include <string_view>
#include <imgui.h>

namespace Engine::ImGuiUtility {

	// 内部の列挙名を日本語の表示名へ変換する
	const char* GetDisplayName(const char* name);
	// 同名の列挙値は型ごとに表示を使い分ける
	const char* GetEnumDisplayName(std::string_view type, const char* name);

	// 列挙値を選択する
	template <typename T>
	bool EnumCombo(const char* label, T* current) noexcept {

		int idx = static_cast<int>(EnumAdapter<T>::GetIndex(*current));
		// 表示名だけを変え、保存する列挙値は維持する
		static const auto names = [] {
			auto values = EnumAdapter<T>::GetEnumArray();
			for (auto& name : values) name = GetEnumDisplayName(magic_enum::enum_type_name<T>(), name);
			return values;
		}();
		bool changed = ImGui::Combo(label, &idx, names.data(),
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
