#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <cstdint>
#include <vector>
#include <utility>
#include <algorithm>
#include <filesystem>
#include <locale>

//============================================================================
//	Algorithm namespace
// 汎用アルゴリズム(列挙→配列化／文字列処理／探索／補間／範囲判定)を提供する
//============================================================================
namespace Engine {
	namespace Algorithm {

		//============================================================================
		//	Enum
		//============================================================================
		// クラス名整形時の先頭大文字/小文字/無加工の指定を行う
		enum class LeadingCase {

			AsIs,  // 変更しない
			Lower, // 先頭を小文字にする
			Upper  // 先頭を大文字にする
		};

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
		//	String
		//============================================================================
		// inputからtoRemoveをすべて取り除いた文字列を返す
		std::string RemoveSubstring(const std::string& input, const std::string& toRemove);

		//	先頭文字の大文字/小文字/無加工を指定どおりに整形する
		std::string AdjustLeadingCase(std::string string, LeadingCase leadingCase);

		// UTF-8のstd::stringをstd::wstringへ変換する
		std::wstring ConvertString(const std::string& str);
		// std::wstringをUTF-8のstd::stringへ変換する
		std::string ConvertString(const std::wstring& wstr);
		// UTF-8のパス文字列をOSのパスへ変換する
		std::filesystem::path PathFromUTF8(const std::string& path);
		// OSのパスをUTF-8文字列へ変換する
		std::string PathToUTF8(const std::filesystem::path& path);

		// ワイド文字列を小文字化して返す
		std::wstring ToLowerW(std::wstring s);
		std::string ToLower(std::string s);

		// ワイド文字列が指定サフィックスで終わるか判定する
		bool EndsWithW(const std::wstring& s, const std::wstring& suf);
		// 文字列が特定のサフィックスで終わるか
		bool EndsWith(const std::string& s, const std::string& suf);

		// haystackにneedleが大小無視で含まれるか、needleが空なら常にtrue
		bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle);

		// UTF-8をUnicodeコードポイント列(char32_t)へ変換
		// 不正なシーケンスはU+FFFDに置換
		std::vector<char32_t> Utf8ToCodepoints(const std::string& s);
		// Unicodeコードポイント列(char32_t)をUTF-8へ変換
		std::string CodepointToUtf8(char32_t cp);

		//============================================================================
		//	Hash
		//============================================================================
		// FNV系の混合でhashへvalueを畳み込む、複数値からハッシュを積み上げる用途
		inline void HashCombine(uint64_t& hash, uint64_t value) {

			hash ^= value;
			hash *= 1099511628211ull;
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
				Assert::Call(false, "not found this object");
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
				Assert::Call(false, "not found this object");
			}

			return found;
		}

	}
}; // Engine
