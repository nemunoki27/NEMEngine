#include "ManagedBuildDiagnosticStore.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cctype>
#include <ctime>

namespace {

	std::string NowTimeString() {
		const std::time_t now = std::time(nullptr);
		std::tm local{};
#if defined(_WIN32)
		localtime_s(&local, &now);
#else
		localtime_r(&now, &local);
#endif
		char buffer[16]{};
		std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
		return std::string(buffer);
	}

	void Truncate(std::string& text, size_t maxLength) {
		if (text.size() > maxLength) {
			text.resize(maxLength);
		}
	}

	// 末尾のMSBuildが付けるproject注記をmessageから取り除く
	void StripTrailingProjectNote(std::string& message) {
		// 末尾空白を削る
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

Engine::ManagedBuildDiagnosticStore& Engine::ManagedBuildDiagnosticStore::GetInstance() {

	static ManagedBuildDiagnosticStore instance;
	return instance;
}

std::optional<Engine::ManagedBuildDiagnostic> Engine::ManagedBuildDiagnosticStore::ParseLine(const std::string& rawLine) {

	// MSBuild や C# compiler の診断行を ": error " ": warning " を境界に左を location 右を code と message として parse する、想定する形は次の 3 種
	//   file(line,col): error CSxxxx: message [proj]
	//   file(line): warning CSxxxx: message
	//   error CSxxxx: message            globalでfile無し
	size_t severityPos = std::string::npos;
	DiagnosticSeverity severity = DiagnosticSeverity::Info;
	size_t severityLen = 0;

	const size_t errorPos = rawLine.find(": error ");
	const size_t warningPos = rawLine.find(": warning ");
	if (errorPos != std::string::npos && (warningPos == std::string::npos || errorPos < warningPos)) {
		severityPos = errorPos;
		severity = DiagnosticSeverity::Error;
		severityLen = 8; // ": error " の長さ
	} else if (warningPos != std::string::npos) {
		severityPos = warningPos;
		severity = DiagnosticSeverity::Warning;
		severityLen = 10; // ": warning " の長さ
	} else {
		// 診断行ではない
		return std::nullopt;
	}

	ManagedBuildDiagnostic diagnostic{};
	diagnostic.severity = severity;
	diagnostic.rawLine = rawLine;
	Truncate(diagnostic.rawLine, kMaxRawLength);

	// 右側はcodeとmessage
	const std::string right = rawLine.substr(severityPos + severityLen);
	const size_t codeEnd = right.find(": ");
	if (codeEnd == std::string::npos) {
		// code区切りが無い形なのでcodeは空にしてmessageに全体を入れる
		diagnostic.message = right;
	} else {
		diagnostic.code = right.substr(0, codeEnd);
		diagnostic.message = right.substr(codeEnd + 2);
	}
	StripTrailingProjectNote(diagnostic.message);
	Truncate(diagnostic.message, kMaxMessageLength);

	// 左側はfileとlineとcol、またはglobalで空
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
			try {
				if (comma == std::string::npos) {
					diagnostic.line = std::stoi(location);
				} else {
					diagnostic.line = std::stoi(location.substr(0, comma));
					diagnostic.column = std::stoi(location.substr(comma + 1));
				}
			}
			catch (...) {
				// locationが数値でない場合はfile扱いをやめてlocationもfileに含める
				diagnostic.file = left;
				Truncate(diagnostic.file, kMaxPathLength);
				diagnostic.line = 0;
				diagnostic.column = 0;
			}
		}
	} else {
		// file locationなし、global error等
		diagnostic.file = left;
		Truncate(diagnostic.file, kMaxPathLength);
	}

	diagnostic.timestamp = NowTimeString();
	return diagnostic;
}

void Engine::ManagedBuildDiagnosticStore::BeginBuild(uint64_t buildId) {

	// 新しいbuildを履歴へ積み、上限超過で最古buildのentryを間引く
	if (buildHistory_.empty() || buildHistory_.back() != buildId) {
		buildHistory_.push_back(buildId);
	}
	while (buildHistory_.size() > kMaxBuildHistory) {
		const uint64_t oldest = buildHistory_.front();
		buildHistory_.pop_front();
		const size_t before = entries_.size();
		entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
			[oldest](const ManagedBuildDiagnostic& d) { return d.buildId == oldest; }), entries_.end());
		if (entries_.size() != before) {
			++version_;
		}
	}
	RecountSeverities();
}

bool Engine::ManagedBuildDiagnosticStore::Ingest(uint64_t buildId, uint64_t reloadId,
	ManagedBuildProcessKind kind, const std::string& rawLine) {

	std::optional<ManagedBuildDiagnostic> parsed = ParseLine(rawLine);
	if (!parsed) {
		return false;
	}
	parsed->buildId = buildId;
	parsed->reloadId = reloadId;
	parsed->processKind = kind;

	if (parsed->severity == DiagnosticSeverity::Error) {
		++errorCount_;
	} else if (parsed->severity == DiagnosticSeverity::Warning) {
		++warningCount_;
	}
	entries_.push_back(std::move(*parsed));
	EnforceBounds();
	++version_;
	return true;
}

void Engine::ManagedBuildDiagnosticStore::EnforceBounds() {

	while (entries_.size() > kMaxEntries) {
		const ManagedBuildDiagnostic& front = entries_.front();
		if (front.severity == DiagnosticSeverity::Error && errorCount_ > 0) {
			--errorCount_;
		} else if (front.severity == DiagnosticSeverity::Warning && warningCount_ > 0) {
			--warningCount_;
		}
		entries_.pop_front();
	}
}

void Engine::ManagedBuildDiagnosticStore::RecountSeverities() {

	errorCount_ = 0;
	warningCount_ = 0;
	for (const ManagedBuildDiagnostic& d : entries_) {
		if (d.severity == DiagnosticSeverity::Error) {
			++errorCount_;
		} else if (d.severity == DiagnosticSeverity::Warning) {
			++warningCount_;
		}
	}
}

void Engine::ManagedBuildDiagnosticStore::Clear() {

	entries_.clear();
	buildHistory_.clear();
	errorCount_ = 0;
	warningCount_ = 0;
	++version_;
}
