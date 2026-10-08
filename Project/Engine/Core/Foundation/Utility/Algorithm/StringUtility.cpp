#include "StringUtility.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cctype>
#include <cwctype>

namespace Engine::Algorithm {

	std::string RemoveSubstring(const std::string& input, const std::string& toRemove) {

		// 空文字は削除しても検索位置が進まない
		if (toRemove.empty()) {
			return input;
		}

		std::string result = input;
		size_t pos;

		// 一致する部分文字列を繰り返し削除
		while ((pos = result.find(toRemove)) != std::string::npos) {
			result.erase(pos, toRemove.length());
		}

		return result;
	}

	std::string AdjustLeadingCase(std::string string, LeadingCase leadingCase) {

		if (string.empty()) {
			return string;
		}
		// 符号なし文字として先頭を変換
		unsigned char ch = static_cast<unsigned char>(string[0]);
		switch (leadingCase) {
		case LeadingCase::Lower:
			string[0] = static_cast<char>(std::tolower(ch));
			break;
		case LeadingCase::Upper:
			string[0] = static_cast<char>(std::toupper(ch));
			break;
		case LeadingCase::AsIs:
		default:
			break;
		}
		return string;
	}

	std::wstring ToLowerW(std::wstring s) {

		// 各文字を小文字に揃える
		std::transform(s.begin(), s.end(), s.begin(),
			[](wchar_t c) -> wchar_t { return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c))); });
		return s;
	}

	std::string ToLower(std::string s) {

		// 各バイトを符号なし文字として小文字へ変換
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	bool EndsWithW(const std::wstring& s, const std::wstring& suf) {

		// 長さを確認して末尾を比較
		return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
	}

	bool EndsWith(const std::string& s, const std::string& suf) {

		// 長さを確認して末尾を比較
		return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
	}

	bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {

		// 大小文字を揃えて部分一致を検索
		if (needle.empty()) {
			return true;
		}
		return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
	}
}
