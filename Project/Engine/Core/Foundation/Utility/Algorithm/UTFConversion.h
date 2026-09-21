#pragma once

//============================================================================
//	include
//============================================================================
#include <string>
#include <vector>

namespace Engine::Algorithm {

	std::wstring ConvertString(const std::string& str);

	std::string ConvertString(const std::wstring& wstr);

	std::vector<char32_t> Utf8ToCodepoints(const std::string& s);

	std::string CodepointToUtf8(char32_t cp);
}
