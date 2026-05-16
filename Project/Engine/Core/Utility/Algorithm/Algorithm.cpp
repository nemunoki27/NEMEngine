#include "Algorithm.h"

using namespace Engine;

//============================================================================
//	Algorithm define
//============================================================================

#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#endif
#include <cstring>
#include <string>
#include <locale>
#include <codecvt>

//============================================================================
//	Algorithm classMethods
//============================================================================

std::string Algorithm::RemoveSubstring(const std::string& input, const std::string& toRemove) {

	std::string result = input;
	size_t pos;

	// toRemoveが見つかる限り削除する
	while ((pos = result.find(toRemove)) != std::string::npos) {
		result.erase(pos, toRemove.length());
	}

	return result;
}

std::string Algorithm::DemangleType(const char* name) {

#if defined(__GNUC__) || defined(__clang__)
	int status = 0;
	size_t len = 0;
	char* demangled = abi::__cxa_demangle(name, nullptr, &len, &status);
	std::string out = (status == 0 && demangled) ? std::string(demangled) : std::string(name);
	std::free(demangled);
	return out;
#else
	return std::string(name);
#endif
}

std::string Algorithm::AdjustLeadingCase(std::string string, LeadingCase leadingCase) {

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

std::wstring Algorithm::ConvertString(const std::string& str) {

	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;
}

std::string Engine::Algorithm::ConvertString(const std::wstring& wstr) {

	if (wstr.empty()) {
		return {};
	}

	const int sizeNeeded = ::WideCharToMultiByte(
		CP_UTF8,
		WC_ERR_INVALID_CHARS,
		wstr.data(),
		static_cast<int>(wstr.size()),
		nullptr,
		0,
		nullptr,
		nullptr
	);

	if (sizeNeeded == 0) {
		throw std::runtime_error("WideCharToMultiByte failed.");
	}

	std::string result(sizeNeeded, '\0');

	const int convertedSize = ::WideCharToMultiByte(
		CP_UTF8,
		WC_ERR_INVALID_CHARS,
		wstr.data(),
		static_cast<int>(wstr.size()),
		result.data(),
		sizeNeeded,
		nullptr,
		nullptr
	);

	if (convertedSize == 0) {
		throw std::runtime_error("WideCharToMultiByte failed.");
	}

	return result;
}

std::wstring Algorithm::ToLowerW(std::wstring s) {

	std::transform(s.begin(), s.end(), s.begin(),
		[](wchar_t c) -> wchar_t {
			return static_cast<wchar_t>(std::tolower(static_cast<wint_t>(c)));
		});
	return s;
}

std::string Algorithm::ToLower(std::string s) {

	std::transform(s.begin(), s.end(), s.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool Algorithm::EndsWithW(const std::wstring& s, const std::wstring& suf) {

	return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool Algorithm::EndsWith(const std::string& s, const std::string& suf) {

	return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

std::vector<char32_t> Algorithm::Utf8ToCodepoints(const std::string& s) {

	std::vector<char32_t> out;
	size_t i = 0;

	while (i < s.size()) {
		uint8_t c = uint8_t(s[i]);

		if (c < 0x80) {
			out.push_back(char32_t(c));
			i += 1;
			continue;
		}

		auto push_repl = [&]() {
			out.push_back(char32_t(0xFFFD));
			};

		if ((c & 0xE0) == 0xC0) {
			if (i + 1 >= s.size()) { push_repl(); break; }
			uint8_t c1 = uint8_t(s[i + 1]);
			if ((c1 & 0xC0) != 0x80) { push_repl(); i += 1; continue; }
			char32_t cp = ((c & 0x1F) << 6) | (c1 & 0x3F);
			out.push_back(cp);
			i += 2;
			continue;
		}

		if ((c & 0xF0) == 0xE0) {
			if (i + 2 >= s.size()) { push_repl(); break; }
			uint8_t c1 = uint8_t(s[i + 1]);
			uint8_t c2 = uint8_t(s[i + 2]);
			if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80)) { push_repl(); i += 1; continue; }
			char32_t cp = ((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) | (c2 & 0x3F);
			out.push_back(cp);
			i += 3;
			continue;
		}

		if ((c & 0xF8) == 0xF0) {
			if (i + 3 >= s.size()) { push_repl(); break; }
			uint8_t c1 = uint8_t(s[i + 1]);
			uint8_t c2 = uint8_t(s[i + 2]);
			uint8_t c3 = uint8_t(s[i + 3]);
			if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80)) {
				push_repl(); i += 1; continue;
			}
			char32_t cp = ((c & 0x07) << 18) | ((c1 & 0x3F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
			out.push_back(cp);
			i += 4;
			continue;
		}

		// 不明
		push_repl();
		i += 1;
	}

	return out;
}

std::string Algorithm::CodepointToUtf8(char32_t cp) {

	std::string out;

	if (cp <= 0x7F) {
		out.push_back(static_cast<char>(cp));
	} else if (cp <= 0x7FF) {
		out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else if (cp <= 0xFFFF) {
		out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	} else {
		out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
	}

	return out;
}

