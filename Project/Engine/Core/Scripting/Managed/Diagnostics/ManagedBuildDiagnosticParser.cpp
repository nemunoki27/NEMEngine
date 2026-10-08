#include "ManagedBuildDiagnosticParser.h"

//============================================================================
//	include
//============================================================================
// c++
#include <charconv>
#include <string_view>

namespace {

	// 保存する文字列を上限へ収める
	void Truncate(std::string& text, size_t maxLength) {

		if (text.size() > maxLength) {
			text.resize(maxLength);
		}
	}

	// 本文末尾のプロジェクト名を除く
	void StripTrailingProjectNote(std::string& message) {

		// 末尾の空白を除く
		while (!message.empty() && (message.back() == ' ' || message.back() == '\t' || message.back() == '\r')) {
			message.pop_back();
		}
		if (!message.empty() && message.back() == ']') {
			const size_t open = message.find_last_of('[');
			if (open != std::string::npos && open > 0 && message[open - 1] == ' ') {
				message.erase(open - 1);
				while (!message.empty() && (message.back() == ' ' || message.back() == '\t')) {
					message.pop_back();
				}
			}
		}
	}
}

//============================================================================
//	ManagedBuildDiagnosticParser functions
//============================================================================
std::optional<Engine::ManagedBuildDiagnostic> Engine::ManagedBuildDiagnosticParser::ParseLine(const std::string& rawLine) {

	using namespace ManagedBuildDiagnosticLimits;

	// 出力行を位置と重大度と本文へ分ける
	size_t severityPos = std::string::npos;
	DiagnosticSeverity severity = DiagnosticSeverity::Info;
	size_t severityLen = 0;

	const size_t errorPos = rawLine.find(": error ");
	const size_t warningPos = rawLine.find(": warning ");
	if (errorPos != std::string::npos && (warningPos == std::string::npos || errorPos < warningPos)) {
		severityPos = errorPos;
		severity = DiagnosticSeverity::Error;
		severityLen = 8; // エラー区切りの文字数
	} else if (warningPos != std::string::npos) {
		severityPos = warningPos;
		severity = DiagnosticSeverity::Warning;
		severityLen = 10; // 警告区切りの文字数
	} else {
		// 診断行ではない
		return std::nullopt;
	}

	ManagedBuildDiagnostic diagnostic{};
	diagnostic.severity = severity;
	diagnostic.rawLine = rawLine;
	Truncate(diagnostic.rawLine, kMaxRawLength);

	// 重大度より後をコードと本文へ分ける
	const std::string right = rawLine.substr(severityPos + severityLen);
	const size_t codeEnd = right.find(": ");
	if (codeEnd == std::string::npos) {
		// コードの区切りがなければ本文だけを保持
		diagnostic.message = right;
	} else {
		diagnostic.code = right.substr(0, codeEnd);
		diagnostic.message = right.substr(codeEnd + 2);
	}
	StripTrailingProjectNote(diagnostic.message);
	Truncate(diagnostic.message, kMaxMessageLength);

	// 重大度より前からファイル名と位置を読む
	std::string left = rawLine.substr(0, severityPos);
	// 行頭の空白を削る
	size_t start = 0;
	while (start < left.size() && (left[start] == ' ' || left[start] == '\t')) {
		++start;
	}
	left = left.substr(start);

	if (!left.empty() && left.back() == ')') {
		const size_t open = left.find_last_of('(');
		if (open != std::string::npos) {
			diagnostic.file = left.substr(0, open);
			Truncate(diagnostic.file, kMaxPathLength);
			const std::string location = left.substr(open + 1, left.size() - open - 2);
			const size_t comma = location.find(',');
			const std::string_view lineText = comma == std::string::npos ?
				std::string_view(location) : std::string_view(location).substr(0, comma);
			const std::string_view columnText = comma == std::string::npos ?
				std::string_view{} : std::string_view(location).substr(comma + 1);
			auto parseInteger = [](std::string_view text, int32_t& out) {

				if (text.empty()) {
					return false;
				}
				const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
				return result.ec == std::errc() && result.ptr == text.data() + text.size();
			};
			if (!parseInteger(lineText, diagnostic.line) ||
				(!columnText.empty() && !parseInteger(columnText, diagnostic.column))) {

				// 位置を解析できなければファイル名へ戻す
				diagnostic.file = left;
				Truncate(diagnostic.file, kMaxPathLength);
				diagnostic.line = 0;
				diagnostic.column = 0;
			}
		}
	} else {
		// 位置がなければ左側全体を保持
		diagnostic.file = left;
		Truncate(diagnostic.file, kMaxPathLength);
	}

	return diagnostic;
}

