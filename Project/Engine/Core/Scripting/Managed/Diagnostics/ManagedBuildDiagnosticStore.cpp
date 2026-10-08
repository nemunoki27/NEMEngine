#include "ManagedBuildDiagnosticStore.h"

//============================================================================
//	include
//============================================================================
#include "ManagedBuildDiagnosticParser.h"

// c++
#include <algorithm>
#include <ctime>
#include <utility>

namespace {

	// 診断を受け取った時刻を文字列にする
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
}

//============================================================================
//	ManagedBuildDiagnosticStore classMethods
//============================================================================
Engine::ManagedBuildDiagnosticStore& Engine::ManagedBuildDiagnosticStore::GetInstance() {

	static ManagedBuildDiagnosticStore instance;
	return instance;
}

std::optional<Engine::ManagedBuildDiagnostic> Engine::ManagedBuildDiagnosticStore::ParseLine(const std::string& rawLine) {

	// 共通の解析結果へ受信時刻を付ける
	auto diagnostic = ManagedBuildDiagnosticParser::ParseLine(rawLine);
	if (diagnostic) {
		diagnostic->timestamp = NowTimeString();
	}
	return diagnostic;
}

void Engine::ManagedBuildDiagnosticStore::BeginBuild(uint64_t buildID) {

	// 上限を超えたビルドの診断を除く
	if (buildHistory_.empty() || buildHistory_.back() != buildID) {
		buildHistory_.push_back(buildID);
	}
	while (buildHistory_.size() > kMaxBuildHistory) {
		const uint64_t oldest = buildHistory_.front();
		buildHistory_.pop_front();
		const size_t before = entries_.size();
		entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
			[oldest](const ManagedBuildDiagnostic& d) { return d.buildID == oldest; }), entries_.end());
		if (entries_.size() != before) {
			++version_;
		}
	}
	RecountSeverities();
}

bool Engine::ManagedBuildDiagnosticStore::Ingest(uint64_t buildID, uint64_t reloadID,
	ManagedBuildProcessKind kind, const std::string& rawLine) {

	// 診断行だけを履歴へ取り込む
	std::optional<ManagedBuildDiagnostic> parsed = ParseLine(rawLine);
	if (!parsed) {
		return false;
	}
	// 呼出し元のビルドと工程を診断へ付ける
	parsed->buildID = buildID;
	parsed->reloadID = reloadID;
	parsed->processKind = kind;

	if (parsed->severity == DiagnosticSeverity::Error) {
		++errorCount_;
	} else if (parsed->severity == DiagnosticSeverity::Warning) {
		++warningCount_;
	}
	// 履歴へ追加し表示の更新を通知
	entries_.push_back(std::move(*parsed));
	EnforceBounds();
	++version_;
	return true;
}

void Engine::ManagedBuildDiagnosticStore::EnforceBounds() {

	// 総件数を超えた古い診断を除く
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

	// 保持中の診断から重大度別件数を集計
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

	// 診断とビルド履歴をまとめて破棄
	entries_.clear();
	buildHistory_.clear();
	errorCount_ = 0;
	warningCount_ = 0;
	++version_;
}
