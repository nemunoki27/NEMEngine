#include "GLTFFileReference.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <stdexcept>

namespace {

	// URIの十六進数を一桁ずつ読む
	int ReadHex(char value) {

		if ('0' <= value && value <= '9') {
			return value - '0';
		}
		if ('a' <= value && value <= 'f') {
			return value - 'a' + 10;
		}
		return 'A' <= value && value <= 'F' ? value - 'A' + 10 : -1;
	}

	// URIへそのまま書ける文字だけを判定する
	bool IsPathCharacter(unsigned char value) {

		return ('a' <= value && value <= 'z') || ('A' <= value && value <= 'Z') ||
			('0' <= value && value <= '9') || value == '-' || value == '_' || value == '.' || value == '~' ||
			value == '/' || 128 <= value;
	}
}

//============================================================================
//	GLTFFileReference functions
//============================================================================
std::filesystem::path Engine::GLTFFileReference::Decode(std::string_view uri) {

	if (uri.find_first_of("?#") != std::string_view::npos || uri.starts_with("//")) {
		throw std::invalid_argument(
			"glTFの外部参照はローカルファイルを指定してください: " + std::string(uri));
	}
	std::string decoded;
	decoded.reserve(uri.size());
	for (size_t index = 0; index < uri.size(); ++index) {

		unsigned char value = static_cast<unsigned char>(uri[index]);
		if (value == '%') {
			// 参照文字列だけを一度復号する
			if (uri.size() - index < 3 || ReadHex(uri[index + 1]) < 0 || ReadHex(uri[index + 2]) < 0) {
				throw std::invalid_argument("glTFの外部参照の文字コードが不正です");
			}
			value = static_cast<unsigned char>(ReadHex(uri[index + 1]) * 16 + ReadHex(uri[index + 2]));
			index += 2;
		}
		if (value < 32 || value == 127) {
			throw std::invalid_argument("glTFの外部参照に制御文字があります");
		}
		decoded.push_back(static_cast<char>(value));
	}
	// ドライブ名以外のschemeとauthorityは取得しない
	const size_t colon = decoded.find(':');
	if ((colon != std::string::npos &&
			(colon != 1 || !(('A' <= decoded[0] && decoded[0] <= 'Z') || ('a' <= decoded[0] && decoded[0] <= 'z')) ||
			 decoded.find(':', colon + 1) != std::string::npos)) || decoded.starts_with("//")) {
		throw std::invalid_argument(
			"glTFの外部参照はローカルファイルを指定してください: " + std::string(uri));
	}
	return Algorithm::PathFromUTF8(decoded);
}

std::string Engine::GLTFFileReference::Encode(const std::filesystem::path& path) {

	constexpr char kHex[] = "0123456789ABCDEF";
	const auto bytes = Algorithm::ConvertString(path.generic_wstring());
	std::string uri;
	uri.reserve(bytes.size());
	for (unsigned char value : bytes) {
		if (value < 32 || value == 127) {
			throw std::invalid_argument("glTFの外部参照に制御文字があります");
		}
		if (IsPathCharacter(value)) {
			uri.push_back(static_cast<char>(value));
		} else {
			// 区切りと日本語を保ち予約文字だけを符号化する
			uri.push_back('%');
			uri.push_back(kHex[value >> 4]);
			uri.push_back(kHex[value & 15]);
		}
	}
	return uri;
}
