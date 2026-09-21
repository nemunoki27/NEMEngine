#pragma once

//============================================================================
//	include
//============================================================================
#include <string>
#include <vector>

namespace Engine::Algorithm {

	enum class LeadingCase {

		AsIs,  // 変更しない
		Lower, // 先頭を小文字にする
		Upper  // 先頭を大文字にする
	};

	std::string RemoveSubstring(const std::string& input, const std::string& toRemove);

	std::string AdjustLeadingCase(std::string string, LeadingCase leadingCase);

	std::wstring ToLowerW(std::wstring s);

	std::string ToLower(std::string s);

	bool EndsWithW(const std::wstring& s, const std::wstring& suf);

	bool EndsWith(const std::string& s, const std::string& suf);

	bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle);
}
