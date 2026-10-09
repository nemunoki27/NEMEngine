#include "FBXDocumentParser.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {

	// 範囲を確認してLittle Endianの整数を読む
	uint64_t ReadInteger(std::string_view bytes, size_t offset, size_t width) {

		if (offset > bytes.size() || width > bytes.size() - offset) {
			throw std::runtime_error("ノードの範囲が文書を超えています");
		}
		uint64_t result = 0;
		for (size_t index = 0; index < width; ++index) {
			result |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[offset + index])) << (index * 8);
		}
		return result;
	}
	// 元の整数幅を超える文書を拒否して位置を保存する
	void WriteInteger(std::string& bytes, size_t offset, size_t width, uint64_t value) {

		if (width == 4 && value > std::numeric_limits<uint32_t>::max()) {
			throw std::runtime_error("32bitの文書サイズを超えています");
		}
		for (size_t index = 0; index < width; ++index) {
			bytes[offset + index] = static_cast<char>(value >> (index * 8));
		}
	}
	// 階層の終端がゼロ埋めか確認する
	void CheckSentinel(std::string_view bytes) {

		if (std::ranges::any_of(bytes, [](char value) { return value != '\0'; })) {
			throw std::runtime_error("ノードの終端が不正です");
		}
	}
	// 画像の文字列以外は圧縮配列もそのまま保持する
	void WriteProperties(std::string_view properties, uint64_t count, bool reference,
		const Engine::GLTFDocumentReferences::ReferenceRewrite& rewrite, std::string& result) {

		if (!reference) {
			result.append(properties);
			return;
		}
		if (count != 1 || properties.size() < 5 || properties.front() != 'S' ||
			ReadInteger(properties, 1, 4) != properties.size() - 5) {
			throw std::runtime_error("画像参照の文字列が不正です");
		}
		std::string path(properties.substr(5));
		Engine::FBXDocumentParser::RewriteReference(path, rewrite);
		const size_t start = result.size();
		result.append(5, '\0');
		result[start] = 'S';
		WriteInteger(result, start + 1, 4, path.size());
		result += path;
	}
	// 子ノードを再構築し絶対位置と参照長を更新する
	void WriteNode(std::string_view bytes, size_t& cursor, size_t limit, size_t width,
		std::string_view parent, size_t depth, const Engine::GLTFDocumentReferences::ReferenceRewrite& rewrite,
		std::string& result) {

		const size_t header = width * 3 + 1;
		if (depth >= 256 || cursor > limit || header > limit - cursor) {
			throw std::runtime_error("ノードの階層または範囲が不正です");
		}
		const uint64_t end = ReadInteger(bytes, cursor, width);
		const uint64_t count = ReadInteger(bytes, cursor + width, width);
		const uint64_t length = ReadInteger(bytes, cursor + width * 2, width);
		const size_t nameLength = static_cast<unsigned char>(bytes[cursor + header - 1]);
		cursor += header;
		if (end > limit || end < cursor || nameLength > end - cursor || length > end - cursor - nameLength) {
			throw std::runtime_error("ノードの位置と参照長が一致しません");
		}
		const auto name = bytes.substr(cursor, nameLength);
		const size_t start = result.size();
		result.append(header, '\0');
		result.back() = static_cast<char>(nameLength);
		result.append(name);
		cursor += nameLength;
		const size_t propertyStart = result.size();
		WriteProperties(bytes.substr(cursor, static_cast<size_t>(length)), count,
			Engine::FBXDocumentParser::IsReference(name, parent), rewrite, result);
		cursor += static_cast<size_t>(length);
		WriteInteger(result, start + width, width, count);
		WriteInteger(result, start + width * 2, width, result.size() - propertyStart);
		if (cursor < end) {
			if (header > end - cursor) {
				throw std::runtime_error("子ノードの終端がありません");
			}
			const size_t childrenEnd = static_cast<size_t>(end) - header;
			while (cursor < childrenEnd) {
				WriteNode(bytes, cursor, childrenEnd, width, name, depth + 1, rewrite, result);
			}
			CheckSentinel(bytes.substr(cursor, header));
			result.append(bytes.substr(cursor, header));
			cursor += header;
		}
		if (cursor != end) {
			throw std::runtime_error("ノードの終端が一致しません");
		}
		WriteInteger(result, start, width, result.size());
	}
}

std::string Engine::FBXDocumentParser::RewriteBinary(
	std::string_view bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite) {

	constexpr std::string_view signature("Kaydara FBX Binary  \0\x1A\0", 23);
	if (!bytes.starts_with(signature)) {
		throw std::runtime_error("バイナリの識別子が不正です");
	}
	const size_t width = ReadInteger(bytes, 23, 4) >= 7500 ? 8 : 4;
	const size_t header = width * 3 + 1;
	std::string result(bytes.substr(0, 27));
	result.reserve(bytes.size());
	size_t cursor = 27;
	while (cursor < bytes.size()) {
		if (ReadInteger(bytes, cursor, width) == 0) {
			if (header > bytes.size() - cursor) {
				throw std::runtime_error("文書の終端がありません");
			}
			CheckSentinel(bytes.substr(cursor, header));
			// 終端以降の署名と付加データは変更しない
			result.append(bytes.substr(cursor));
			return result;
		}
		WriteNode(bytes, cursor, bytes.size(), width, {}, 0, rewrite, result);
	}
	throw std::runtime_error("文書の終端がありません");
}
