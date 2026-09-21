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

		//============================================================================
		//	Enum
		//============================================================================
		// 列挙の0..(enumValue-1)をuint32配列として取得する
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
		// メンバfindを持つ連想系コンテナでキーの存在を判定する、必要に応じAssert::Call
		template <typename, typename = std::void_t<>>
		struct has_find_method : std::false_type {};
		template <typename T>
		struct has_find_method<T, std::void_t<decltype(std::declval<T>().find(std::declval<typename T::key_type>()))>>
			: std::true_type {
		};
		template <typename T>
		constexpr bool has_find_method_v = has_find_method<T>::value;

		// 連想コンテナに対しkeyの存在を返す、assertionEnable時に未発見ならAssert::Call
		template <typename TA, typename TB>
		typename std::enable_if_t<has_find_method_v<TA>, bool>
			Find(const TA& object, const TB& key, bool assertionEnable = false) {

			auto it = object.find(key);
			bool found = it != object.end();

			if (!found && assertionEnable) {
				Assert::Call(false, "対象オブジェクトが見つかりません");
			}
			return found;
		}
		// シーケンスコンテナに対しkeyの存在を返す、assertionEnable時に未発見ならAssert::Call
		template <typename TA, typename TB>
		typename std::enable_if_t<!has_find_method_v<TA>, bool>
			Find(const TA& object, const TB& key, bool assertionEnable = false) {

			auto it = std::find(object.begin(), object.end(), key);
			bool found = it != object.end();

			if (!found && assertionEnable) {
				Assert::Call(false, "対象オブジェクトが見つかりません");
			}

			return found;
		}

	}
}; // Engine
