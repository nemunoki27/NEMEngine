#include "UTFConversion.h"

//============================================================================
//	include
//============================================================================
#include <cstdint>
#include <stdexcept>
#include <limits>

// Windows
#include <Windows.h>

namespace {

	// UTF8からUTF16への変換手順を共有する
	std::wstring ToWide(const std::string& text, bool rejectInvalid) {

		if (text.empty()) {
			return {};
		}
		// OSへ渡せる文字列長を確認
		if (text.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
			throw std::length_error("UTF8文字列が長すぎます");
		}
		const DWORD flags = rejectInvalid ? MB_ERR_INVALID_CHARS : 0;
		const int length = static_cast<int>(text.size());
		const int size = MultiByteToWideChar(CP_UTF8, flags, text.data(), length, nullptr, 0);
		if (size == 0) {
			if (rejectInvalid) {
				throw std::runtime_error("UTF8からUTF16へ変換できません");
			}
			return {};
		}
		// 必要な領域へ変換し、書込結果も確認
		std::wstring result(static_cast<size_t>(size), L'\0');
		if (MultiByteToWideChar(CP_UTF8, flags, text.data(), length, result.data(), size) != size) {
			if (rejectInvalid) {
				throw std::runtime_error("UTF8からUTF16へ変換できません");
			}
			return {};
		}
		return result;
	}
}

namespace Engine::Algorithm {

	std::wstring ConvertString(const std::string& str) {

		// 表示用文字列は不正な列を置換
		return ToWide(str, false);
	}

	std::wstring ConvertStringStrict(const std::string& str) {

		// ファイル名などの不正な列を拒否
		return ToWide(str, true);
	}

	std::string ConvertString(const std::wstring& wstr) {

		if (wstr.empty()) {
			return {};
		}

		if (wstr.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
			throw std::length_error("UTF16文字列が長すぎます");
		}
		// 必要なUTF8領域の大きさを取得
		const int sizeNeeded = ::WideCharToMultiByte(
			CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(), static_cast<int>(wstr.size()), nullptr, 0, nullptr, nullptr);

		if (sizeNeeded == 0) {
			throw std::runtime_error("UTF16からUTF8へ変換できません");
		}

		std::string result(sizeNeeded, '\0');

		// 変換後の書込結果を確認
		const int convertedSize = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wstr.data(),
			static_cast<int>(wstr.size()), result.data(), sizeNeeded, nullptr, nullptr);

		if (convertedSize != sizeNeeded) {
			throw std::runtime_error("UTF16からUTF8へ変換できません");
		}

		return result;
	}

	std::vector<char32_t> UTF8ToCodepoints(const std::string& text) {

		// 先頭バイトから文字単位で展開
		std::vector<char32_t> result;
		for (size_t index = 0; index < text.size();) {
			const uint8_t lead = static_cast<uint8_t>(text[index]);
			if (lead < 0x80) {
				result.push_back(lead);
				++index;
				continue;
			}
			const size_t count = lead >= 0xC2 && lead <= 0xDF	? 2
								 : lead >= 0xE0 && lead <= 0xEF ? 3
								 : lead >= 0xF0 && lead <= 0xF4 ? 4
																: 0;
			char32_t codepoint = count == 2 ? lead & 0x1F : count == 3 ? lead & 0x0F : lead & 0x07;
			bool valid = count != 0 && count <= text.size() - index;
			for (size_t offset = 1; valid && offset < count; ++offset) {
				const uint8_t next = static_cast<uint8_t>(text[index + offset]);
				valid = (next & 0xC0) == 0x80;
				codepoint = (codepoint << 6) | (next & 0x3F);
			}
			// 過長表現とサロゲートと範囲外を除外
			valid = valid && codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF) &&
					(count != 2 || codepoint >= 0x80) && (count != 3 || codepoint >= 0x800) &&
					(count != 4 || codepoint >= 0x10000);
			result.push_back(valid ? codepoint : char32_t{0xFFFD});
			// 不正バイトだけを進めて後続文字を維持
			index += valid ? count : 1;
		}
		return result;
	}

	std::string CodepointToUTF8(char32_t cp) {

		// Unicodeの範囲外は置換文字へ戻す
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
			cp = 0xFFFD;
		}
		std::string out;

		// 文字コードの範囲に応じてバイト列を作成
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
