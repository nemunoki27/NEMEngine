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

		// toRemoveが見つかる限り削除する
		while ((pos = result.find(toRemove)) != std::string::npos) {
			result.erase(pos, toRemove.length());
		}

		return result;
	}

	std::string AdjustLeadingCase(std::string string, LeadingCase leadingCase) {

		if (string.empty()) {
			return string;
		}
		unsigned char ch = static_cast<unsigned char>(string[0]);
		switch (leadingCase) {
		case LeadingCase::Lower: string[0] = static_cast<char>(std::tolower(ch)); break;
		case LeadingCase::Upper: string[0] = static_cast<char>(std::toupper(ch)); break;
		case LeadingCase::AsIs:  default: break;
		}
		return string;
	}

	std::wstring ToLowerW(std::wstring s) {

		std::transform(s.begin(), s.end(), s.begin(),
			[](wchar_t c) -> wchar_t {
				return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
		});
		return s;
	}

	std::string ToLower(std::string s) {

		std::transform(s.begin(), s.end(), s.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return s;
	}

	bool EndsWithW(const std::wstring& s, const std::wstring& suf) {

		return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
	}

	bool EndsWith(const std::string& s, const std::string& suf) {

		return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
	}

	bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle) {

		if (needle.empty()) {
			return true;
		}
		return ToLower(haystack).find(ToLower(needle)) != std::string::npos;
	}
}
