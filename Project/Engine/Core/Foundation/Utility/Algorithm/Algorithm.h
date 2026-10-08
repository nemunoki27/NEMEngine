#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include "StringUtility.h"
#include "UTFConversion.h"
#include "PathUtility.h"
#include "HashUtility.h"

// c++
#include <cstdint>
#include <vector>
#include <utility>
#include <algorithm>
#include <filesystem>
#include <type_traits>

//============================================================================
//	Algorithm namespace
//	列挙・探索と用途別の共通処理を提供する
//============================================================================
namespace Engine {
	namespace Algorithm {

		// 指定要素を移動し、間の要素順を維持する
		template <typename T>
		void MoveListItem(std::vector<T>& list, int32_t from, int32_t to) {

			if (from < 0 || to < 0 || list.size() <= static_cast<size_t>(from) || list.size() <= static_cast<size_t>(to)) {
				return;
			}
			if (from < to) {
				std::rotate(list.begin() + from, list.begin() + from + 1, list.begin() + to + 1);
			} else {
				std::rotate(list.begin() + to, list.begin() + from, list.begin() + from + 1);
			}
		}

		//============================================================================
		//	Enum
		//============================================================================
		// 先頭から指定した列挙値の直前まで取得する
		template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
		std::vector<uint32_t> GetEnumArray(Enum enumValue) {

			std::vector<uint32_t> intValues;
			for (uint32_t i = 0; i < static_cast<uint32_t>(enumValue); ++i) {

				intValues.push_back(i);
			}
			return intValues;
		}

		// 列挙値valueがフラグflagを持つか判定する
		template <typename Enum, typename = std::enable_if_t<std::is_enum_v<Enum>>>
		constexpr bool HasFlag(Enum value, Enum flag) {

			using U = std::underlying_type_t<Enum>;
			return (static_cast<U>(value) & static_cast<U>(flag)) != 0;
		}

		//============================================================================
		//	Find
		//============================================================================
		// コンテナがキーによる探索を持つか判定する
		template <typename, typename = std::void_t<>>
		struct has_find_method : std::false_type {};
		template <typename T>
		struct has_find_method<T, std::void_t<decltype(std::declval<T>().find(std::declval<typename T::key_type>()))>>
			: std::true_type {};
		template <typename T>
		constexpr bool has_find_method_v = has_find_method<T>::value;

		// コンテナからキーを探索する
		template <typename TA, typename TB>
		bool Find(const TA& object, const TB& key, bool assertionEnable = false) {

			// コンテナの探索方法を選択
			const bool found = [&]() {
				if constexpr (has_find_method_v<TA>) {
					return object.find(key) != object.end();
				} else {
					return std::find(object.begin(), object.end(), key) != object.end();
				}
			}();
			// 指定された場合だけ未発見を通知
			if (!found && assertionEnable) {
				Assert::Call(false, "対象オブジェクトが見つかりません");
			}
			return found;
		}

	}
}
