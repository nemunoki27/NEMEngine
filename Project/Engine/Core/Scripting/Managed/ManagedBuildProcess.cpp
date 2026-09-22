#include "ManagedBuildProcess.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

#include <cctype>

namespace {
	spdlog::level::level_enum DiagnosticLogLevel(const std::string& line) {

		if (const auto diagnostic = Engine::ManagedBuildDiagnosticStore::ParseLine(line)) {

			switch (diagnostic->severity) {
			case Engine::DiagnosticSeverity::Error:   return spdlog::level::err;
			case Engine::DiagnosticSeverity::Warning: return spdlog::level::warn;
			default:                                  return spdlog::level::info;
			}
		}
		return spdlog::level::info;
	}

	bool ContainsErrorToken(const std::string& line) {

		std::string lower;
		lower.reserve(line.size());
		for (char c : line) {
			lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
		}
		// 集計行は実エラー本文ではないので除外し実際のエラー行だけを対象にする
		if (lower.find("error(s)") != std::string::npos) {
			return false;
		}
		return lower.find("error") != std::string::npos;
	}
}

bool Engine::ManagedBuildProcess::Start(const std::wstring& command, const std::filesystem::path& directory) {

	return process_.Start(command, directory);
}

void Engine::ManagedBuildProcess::Terminate() {

	process_.Terminate();
}

void Engine::ManagedBuildProcess::ResetDiagnostics() {

	firstErrorLine_.clear();
	lastErrorLine_.clear();
}

int32_t Engine::ManagedBuildProcess::ExitCode() const {

	return process_.ExitCode();
}

bool Engine::ManagedBuildProcess::PollMetadataSync(uint64_t buildID, uint64_t reloadID) {

	return process_.Poll([this, buildID, reloadID](const std::string& line) {
			// 同期ツールの出力つまり採番やリネームや曖昧診断をエディタコンソールへ転送する、警告/エラーは色分けされる
			Logger::Output(LogType::GameLogic, DiagnosticLogLevel(line), "[スクリプトメタ同期] {}", line);
			// 取り込み点で構造化診断ストアへ入れコンソール文字列は再解析しない
			ManagedBuildDiagnosticStore::GetInstance().Ingest(buildID, reloadID,
				ManagedBuildProcessKind::MetadataSync, line);
			if (ContainsErrorToken(line)) {
				if (firstErrorLine_.empty()) {
					firstErrorLine_ = line;
				}
				lastErrorLine_ = line;
			}
			});
}

bool Engine::ManagedBuildProcess::PollBuild(uint64_t buildID, uint64_t reloadID) {

	return process_.Poll([this, buildID, reloadID](const std::string& line) {
			// ビルド出力をエディタコンソールつまりGameLogicログへ逐次転送する、警告は黄エラーは赤で色分けされる
			Logger::Output(LogType::GameLogic, DiagnosticLogLevel(line), "[ゲームスクリプトビルド] {}", line);
			// 取り込み点で構造化診断ストアへ入れMSBuildやCSCのエラーと警告を解析する
			ManagedBuildDiagnosticStore::GetInstance().Ingest(buildID, reloadID,
				ManagedBuildProcessKind::Build, line);
			// engine.log要約用にエラー行の最初と最後を保持する、全文はgameLogic.log側
			if (ContainsErrorToken(line)) {
				if (firstErrorLine_.empty()) {
					firstErrorLine_ = line;
				}
				lastErrorLine_ = line;
			}
			});
}
