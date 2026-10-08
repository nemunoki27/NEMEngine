#pragma once

//============================================================================
//	include
//============================================================================
#include <string>

namespace Engine::Algorithm {

	enum class LeadingCase {

		AsIs,  // 変更しない
		Lower, // 先頭を小文字にする
		Upper  // 先頭を大文字にする
	};

	// 一致する部分文字列を削除する
	std::string RemoveSubstring(const std::string& input, const std::string& toRemove);

	// 先頭文字の大文字と小文字を揃える
	std::string AdjustLeadingCase(std::string string, LeadingCase leadingCase);

	// UTF16文字列を小文字に揃える
	std::wstring ToLowerW(std::wstring s);

	// 文字列を小文字に揃える
	std::string ToLower(std::string s);

	// UTF16文字列の末尾を比較する
	bool EndsWithW(const std::wstring& s, const std::wstring& suf);

	// 文字列の末尾を比較する
	bool EndsWith(const std::string& s, const std::string& suf);

	// 大文字と小文字を区別せず部分一致を調べる
	bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle);
}
