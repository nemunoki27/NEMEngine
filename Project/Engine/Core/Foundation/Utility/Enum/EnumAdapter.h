#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>
// magic_enum
#include <magic_enum.hpp>

//============================================================================
//	EnumAdapter class
//	magic_enumを利用したenumをstringやindexに変換するアダプター
//============================================================================
namespace Engine {

	template <typename T>
	class EnumAdapter {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		static_assert(std::is_enum_v<T>, "EnumAdapter には enum 型を渡してください");

		using Enum = T;
		using Underlying = std::underlying_type_t<T>;

		// enumの列挙数
		static constexpr size_t GetEnumCount() noexcept {

			return magic_enum::enum_count<T>();
		}

		// index番目の名前を取得
		static constexpr const char* GetEnumName(uint32_t index) noexcept {

			constexpr auto names = magic_enum::enum_names<T>();
			return index < names.size() ? names[index].data() : "";
		}

		// 全ての列挙名を取得
		static constexpr std::array<const char*, magic_enum::enum_count<T>()> GetEnumArray() noexcept {

			constexpr auto names = magic_enum::enum_names<T>();
			constexpr auto arr = [] {
				std::array<const char*, magic_enum::enum_count<T>()> tmp{};
				for (std::size_t i = 0; i < tmp.size(); ++i)
					tmp[i] = names[i].data();
				return tmp;
				}();
			return arr;
		}

		static constexpr Enum GetValue(std::uint32_t index) noexcept {

			constexpr auto values = magic_enum::enum_values<T>();
			return index < values.size() ? values[index] : static_cast<T>(0);
		}

		static constexpr uint32_t GetIndex(Enum value) noexcept {

			auto idx = magic_enum::enum_index<T>(value);
			return idx ? static_cast<uint32_t>(*idx) : 0;
		}

		static constexpr const char* ToString(Enum value) noexcept {

			const auto name = magic_enum::enum_name(value);
			return name.empty() ? "" : name.data();
		}

		static constexpr std::string_view ToStringView(Enum value) noexcept {

			return magic_enum::enum_name(value);
		}

		static constexpr std::optional<Enum> FromString(std::string_view name) noexcept {

			return magic_enum::enum_cast<T>(name);
		}

	};
}; // Engine
