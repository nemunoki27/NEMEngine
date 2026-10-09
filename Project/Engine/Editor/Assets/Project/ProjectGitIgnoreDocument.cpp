#include "ProjectGitIgnoreDocument.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace {

	constexpr std::string_view kBegin = "# NEMEngine Project exclusions begin";
	constexpr std::string_view kEnd = "# NEMEngine Project exclusions end";

	// Gitのパターン記号と末尾空白を文字として扱う
	std::string EscapePath(const std::string& path) {

		std::string result = "/";
		for (const char value : path) {
			if (std::string_view("\\*?[] !#").find(value) != std::string_view::npos) {
				result += '\\';
			}
			result += value;
		}
		return result;
	}
}

bool Engine::ProjectGitIgnoreDocument::Update(const std::string& content, const std::string& relativePath,
	bool directory, bool included, std::string& updated, std::string& error) {

	error.clear();
	updated.clear();
	// Repository内の相対パスだけを受け付ける
	if (relativePath.empty() || relativePath.front() == '/' || relativePath.back() == '/' ||
		relativePath.find_first_of("\\\r\n\0", 0, 4) != std::string::npos) {
		error = "Gitの対象パスが不正です";
		return false;
	}
	for (size_t begin = 0; begin < relativePath.size();) {
		const size_t end = relativePath.find('/', begin);
		const auto part = relativePath.substr(begin, end - begin);
		if (part.empty() || part == "." || part == ".." || part == ".git") {
			error = "Gitの対象パスが不正です";
			return false;
		}
		begin = end == std::string::npos ? relativePath.size() : end + 1;
	}

	size_t blockBegin = std::string::npos;
	size_t blockEnd = std::string::npos;
	bool inside = false;
	bool malformed = false;
	std::vector<std::string> rules;
	// Editorの設定区間以外はbyte単位で残す
	for (size_t offset = 0; offset < content.size();) {
		const size_t newline = content.find('\n', offset);
		const size_t next = newline == std::string::npos ? content.size() : newline + 1;
		auto line = content.substr(offset, (newline == std::string::npos ? content.size() : newline) - offset);
		if (!line.empty() && line.back() == '\r') { line.pop_back(); }
		if (line == kBegin) {
			if (blockBegin != std::string::npos) { malformed = true; break; }
			blockBegin = offset;
			inside = true;
		} else if (line == kEnd) {
			if (!inside || blockEnd != std::string::npos) { malformed = true; break; }
			blockEnd = next;
			inside = false;
		} else if (inside) {
			rules.emplace_back(std::move(line));
		}
		offset = next;
	}
	if (malformed || inside || (blockBegin == std::string::npos) != (blockEnd == std::string::npos)) {
		error = ".gitignoreのEditor設定区間が壊れています";
		return false;
	}
	const std::string path = EscapePath(relativePath);
	const std::string target = path + (directory ? "/" : "");
	const std::string meta = path + ".meta";
	const std::array<std::string, 4> ownRules = { target, meta, "!" + target, "!" + meta };
	std::erase_if(rules, [&](const std::string& rule) {
		return std::find(ownRules.begin(), ownRules.end(), rule) != ownRules.end();
	});
	// 本体とmetaの設定を一緒に切り替える
	rules.emplace_back((included ? "!" : "") + target);
	rules.emplace_back((included ? "!" : "") + meta);
	const std::string newline = content.find("\r\n") != std::string::npos ? "\r\n" : "\n";
	std::string block = std::string(kBegin) + newline;
	for (const auto& rule : rules) { block += rule + newline; }
	block += std::string(kEnd) + newline;
	if (blockBegin == std::string::npos) {
		updated = content;
		if (!updated.empty() && updated.back() != '\n') { updated += newline; }
		updated += block;
	} else {
		updated = content.substr(0, blockBegin) + block + content.substr(blockEnd);
	}
	return true;
}
