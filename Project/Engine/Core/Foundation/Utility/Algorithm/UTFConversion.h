#pragma once

//============================================================================
//	include
//============================================================================
#include <string>
#include <vector>

namespace Engine::Algorithm {

	// 不正な文字を置換してUTF16へ変換する
	std::wstring ConvertString(const std::string& str);
	// 不正なUTF8を拒否してUTF16へ変換する
	std::wstring ConvertStringStrict(const std::string& str);

	// 不正なUTF16を拒否してUTF8へ変換する
	std::string ConvertString(const std::wstring& wstr);

	// 不正な列を置換して文字コードへ展開する
	std::vector<char32_t> UTF8ToCodepoints(const std::string& text);

	// 不正な文字コードを置換してUTF8へ変換する
	std::string CodepointToUTF8(char32_t cp);
}
