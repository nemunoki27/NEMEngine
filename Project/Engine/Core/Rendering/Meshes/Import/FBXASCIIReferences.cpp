#include "FBXDocumentParser.h"

//============================================================================
//	include
//============================================================================
// c++
#include <stdexcept>
#include <vector>

namespace {

	// FBXの空白と行末を判定する
	bool IsSpace(char value) {

		return value == ' ' || value == '\t' || value == '\r' || value == '\n';
	}
}

std::string Engine::FBXDocumentParser::RewriteASCII(
	std::string_view bytes, const GLTFDocumentReferences::ReferenceRewrite& rewrite) {

	std::string result;
	result.reserve(bytes.size());
	std::vector<std::string_view> parents;
	std::string_view name;
	size_t copied = 0;
	size_t cursor = 0;
	while (cursor < bytes.size()) {
		const char value = bytes[cursor];
		// コメントと文字列内の括弧は階層に含めない
		if (value == ';') {
			const auto end = bytes.find_first_of("\r\n", cursor);
			cursor = end == std::string_view::npos ? bytes.size() : end;
		} else if (value == '"') {
			const size_t start = ++cursor;
			const auto end = bytes.find('"', start);
			if (end == std::string_view::npos) {
				throw std::runtime_error("文字列の終端がありません");
			}
			if (!parents.empty() && IsReference(name, parents.back())) {
				std::string reference(bytes.substr(start, end - start));
				RewriteReference(reference, rewrite);
				result.append(bytes.substr(copied, start - copied));
				result += reference;
				copied = end;
			}
			cursor = end + 1;
		} else if (value == '{') {
			if (parents.size() >= 256) {
				throw std::runtime_error("階層が深すぎます");
			}
			parents.push_back(name);
			name = {};
			++cursor;
		} else if (value == '}') {
			if (parents.empty()) {
				throw std::runtime_error("階層の終端が一致しません");
			}
			parents.pop_back();
			name = {};
			++cursor;
		} else if (IsSpace(value) || value == ',' || value == ':') {
			++cursor;
		} else {
			// コロン直前の名前だけをノード名として保持する
			const size_t start = cursor;
			while (cursor < bytes.size() && !IsSpace(bytes[cursor]) &&
				std::string_view(":,{};\"").find(bytes[cursor]) == std::string_view::npos) {
				++cursor;
			}
			const size_t end = cursor;
			while (cursor < bytes.size() && IsSpace(bytes[cursor])) {
				++cursor;
			}
			if (cursor < bytes.size() && bytes[cursor] == ':') {
				name = bytes.substr(start, end - start);
				++cursor;
			}
		}
	}
	if (!parents.empty()) {
		throw std::runtime_error("階層が閉じられていません");
	}
	result.append(bytes.substr(copied));
	return result;
}
