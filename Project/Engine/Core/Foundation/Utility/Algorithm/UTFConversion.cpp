#include "UTFConversion.h"

//============================================================================
//	include
//============================================================================
#include <cstdint>
#include <stdexcept>
#include <limits>
#include <Windows.h>

namespace Engine::Algorithm {

	std::wstring ConvertString(const std::string& str) {

		if (str.empty()) {
			return std::wstring();
		}

		if (str.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
			throw std::length_error("UTF8 string is too long");
		}
		auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
		if (sizeNeeded == 0) {
			return std::wstring();
		}
		std::wstring result(sizeNeeded, 0);
		MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
		return result;
	}

	std::string ConvertString(const std::wstring& wstr) {

		if (wstr.empty()) {
			return {};
		}

		if (wstr.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
			throw std::length_error("UTF16 string is too long");
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

	std::vector<char32_t> Utf8ToCodepoints(const std::string& text) {

		std::vector<char32_t> result;
		for (size_t index = 0; index < text.size();) {
			const uint8_t lead = static_cast<uint8_t>(text[index]);
			if (lead < 0x80) {
				result.push_back(lead);
				++index;
				continue;
			}
			const size_t count = lead >= 0xC2 && lead <= 0xDF ? 2 :
				lead >= 0xE0 && lead <= 0xEF ? 3 : lead >= 0xF0 && lead <= 0xF4 ? 4 : 0;
			char32_t codepoint = count == 2 ? lead & 0x1F : count == 3 ? lead & 0x0F : lead & 0x07;
			bool valid = count != 0 && count <= text.size() - index;
			for (size_t offset = 1; valid && offset < count; ++offset) {
				const uint8_t next = static_cast<uint8_t>(text[index + offset]);
				valid = (next & 0xC0) == 0x80;
				codepoint = (codepoint << 6) | (next & 0x3F);
			}
			// 過長表現、surrogate、Unicode範囲外を除外する
			valid = valid && codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF) &&
				(count != 2 || codepoint >= 0x80) && (count != 3 || codepoint >= 0x800) && (count != 4 || codepoint >= 0x10000);
			result.push_back(valid ? codepoint : char32_t{ 0xFFFD });
			// 不正byteだけを進め、後続の正常文字を取りこぼさない
			index += valid ? count : 1;
		}
		return result;
	}

	std::string CodepointToUtf8(char32_t cp) {

		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
			cp = 0xFFFD;
		}
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
}
