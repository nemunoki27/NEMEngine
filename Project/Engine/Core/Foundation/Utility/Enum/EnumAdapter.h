#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
// magic_enum
#include <magic_enum.hpp>

//============================================================================
//	EnumAdapter class
//	列挙型の名前と値と位置を相互変換する
//============================================================================
namespace Engine {

	template <typename T>
	class EnumAdapter {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		static_assert(std::is_enum_v<T>, "EnumAdapter には enum 型を渡してください");

		using Enum = T;
		using Underlying = std::underlying_type_t<T>;

		//--------- functions ----------------------------------------------------

		// 列挙値の総数を取得する
		static constexpr size_t GetEnumCount() noexcept { return magic_enum::enum_count<T>(); }

		// 指定した位置の列挙名を取得する
		static constexpr const char* GetEnumName(uint32_t index) noexcept {

			constexpr auto names = magic_enum::enum_names<T>();
			return index < names.size() ? names[index].data() : "";
		}

		// 全ての列挙名を取得
		static constexpr std::array<const char*, magic_enum::enum_count<T>()> GetEnumArray() noexcept {

			constexpr auto arr = [] {
				constexpr auto names = magic_enum::enum_names<T>();
				std::array<const char*, magic_enum::enum_count<T>()> tmp{};
				for (std::size_t i = 0; i < tmp.size(); ++i) {
					tmp[i] = names[i].data();
				}
				return tmp;
			}();
			return arr;
		}

		// 指定した位置の列挙値を取得する
		static constexpr Enum GetValue(std::uint32_t index) noexcept {

			constexpr auto values = magic_enum::enum_values<T>();
			return index < values.size() ? values[index] : static_cast<T>(0);
		}

		// 列挙値の位置を取得する
		static constexpr uint32_t GetIndex(Enum value) noexcept {

			auto idx = magic_enum::enum_index<T>(value);
			return idx ? static_cast<uint32_t>(*idx) : 0;
		}

		// 列挙値の名前を取得する
		static constexpr const char* ToString(Enum value) noexcept {

			const auto name = magic_enum::enum_name(value);
			return name.empty() ? "" : name.data();
		}

		// 列挙値の名前を借用する
		static constexpr std::string_view ToStringView(Enum value) noexcept { return magic_enum::enum_name(value); }

		// 名前に対応する列挙値を取得する
		static constexpr std::optional<Enum> FromString(std::string_view name) noexcept {

			return magic_enum::enum_cast<T>(name);
		}
	};
}
