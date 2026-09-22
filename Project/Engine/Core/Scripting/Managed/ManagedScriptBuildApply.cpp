#include "ManagedScriptBuildService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedScriptRuntime.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedBuildDiagnosticStore.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// windowsの_wcsicmp等
#include <cwctype>
// c++
#include <algorithm>
#include <cctype>
#include <ctime>
#include <vector>

#include "ManagedBuildUtility.h"

using namespace Engine::ManagedBuildUtility;

namespace {
	constexpr const wchar_t* kAssemblyFileName = L"GameScripts.dll";
	constexpr const wchar_t* kManifestFileName = L"GameScripts.scriptmanifest.json";
}

void Engine::ManagedScriptBuildService::OnBuildFinished() {

	const auto buildEnd = std::chrono::steady_clock::now();
	cycle_.diagnostics.buildMs = DurationMs(cycle_.buildStartTime, buildEnd);
	cycle_.diagnostics.buildExitCode = process_.ExitCode();

	if (cycle_.diagnostics.buildExitCode != 0) {

		// ビルド失敗時は正常DLLを解放せず維持する、全文はgameLogic.logでengine.logには要約を残す
		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: ビルドに失敗したため現在のAssemblyを維持します 終了Code={} BuildID={}",
			cycle_.diagnostics.buildExitCode, cycle_.diagnostics.buildID);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  作業Directory={}", ToUtf8Path(cycle_.lastBuildWorkingDir));
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  実行Command={}", cycle_.lastBuildCommandUtf8);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  Staging先={}", ToUtf8Path(cycle_.currentStagingDir));
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  最初のError={}", process_.FirstError().empty() ? "取得なし" : process_.FirstError());
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  最後のError={}", process_.LastError().empty() ? "取得なし" : process_.LastError());
		Logger::Output(LogType::Engine, spdlog::level::err,
			"  ビルド出力全体はgameLogic.logにあります");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}

	// ステージング成果物の検証
	cycle_.diagnostics.artifactValid = artifacts_.ValidateArtifacts(cycle_.currentStagingDir);
	if (!cycle_.diagnostics.artifactValid) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: Staging成果物の検証に失敗したため現在のAssemblyを維持します");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}
	SetState(State::BuildSucceeded);

	// マニフェストをステージングへ生成する、対象DLLは一時ALCで読むだけで現行DLLに触れず検証失敗時はロードしない
	const auto manifestStart = std::chrono::steady_clock::now();
	const std::filesystem::path stagedDll = cycle_.currentStagingDir / kAssemblyFileName;
	const std::filesystem::path stagedManifest = cycle_.currentStagingDir / kManifestFileName;
	const ManagedStatus manifestStatus = runtime_->GenerateScriptManifest(stagedDll, stagedManifest);
	cycle_.diagnostics.manifestMs = DurationMs(manifestStart, std::chrono::steady_clock::now());
	cycle_.diagnostics.manifestValid = (manifestStatus == ManagedStatus::Ok);
	if (!cycle_.diagnostics.manifestValid) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: Script Manifestの生成または検証に失敗したため現在のAssemblyを維持します "
			"状態={}", static_cast<int32_t>(manifestStatus));
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}

	// シャドウコピーを作成しステージングからコピーする
	const auto stagingStart = std::chrono::steady_clock::now();
	SetState(State::Staging);
	cycle_.diagnostics.reloadID = ++reloadCounter_;
	cycle_.currentShadowDir = artifacts_.ShadowRoot() / std::to_wstring(cycle_.diagnostics.reloadID);
	std::error_code dirError{};
	std::filesystem::create_directories(cycle_.currentShadowDir, dirError);

	// dll/pdb/deps/runtimeconfigと一緒にマニフェストもシャドウへコピーされる
	if (!artifacts_.CopyArtifacts(cycle_.currentStagingDir, cycle_.currentShadowDir) || !artifacts_.ValidateArtifacts(cycle_.currentShadowDir)) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: Shadow Copy作成に失敗したため現在のAssemblyを維持します");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}

	// ロード前にシャドウにマニフェストが確実に存在するか確認し揃っていなければロードしない
	std::error_code shadowManifestExists{};
	if (!std::filesystem::exists(cycle_.currentShadowDir / kManifestFileName, shadowManifestExists) || shadowManifestExists) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"ManagedScriptBuildService: Shadow CopyにScript Manifestがないため現在のAssemblyを維持します");
		SetState(State::BuildFailed);
		FinishCycle(false);
		return;
	}
	cycle_.diagnostics.shadowCopyMs = DurationMs(stagingStart, std::chrono::steady_clock::now());

	// 実際の解放とロードは次のTickで行う、メインスレッドかつEdit確認後
	SetState(State::ReloadPending);
}

void Engine::ManagedScriptBuildService::ApplyReload() {

	SetState(State::Reloading);

	const auto loadStart = std::chrono::steady_clock::now();
	const std::filesystem::path shadowDll = cycle_.currentShadowDir / kAssemblyFileName;

	// 現在のアセンブリを解放しシャドウコピーから回収可能なALCへロードする
	const bool loaded = runtime_->LoadGameAssemblyFromPath(shadowDll);
	cycle_.diagnostics.loadMs = DurationMs(loadStart, std::chrono::steady_clock::now());
	cycle_.diagnostics.scriptTypeCount = runtime_->ManagedScriptTypeCount();

	// 旧アセンブリのALC解放の状態をログ解析せず取り込む、リロード経路のみでUnknownは正常扱いしない
	alcUnloadStatus_ = runtime_->GetLastAlcUnloadStatus();
	alcLeakSuspected_ = (alcUnloadStatus_ == AlcUnloadStatus::LeakSuspected);

	if (loaded) {

		SetState(State::ReloadSucceeded);
		// スナップショット用に最終成功時刻を記録する、ログ文字列ではなく構造化状態として保持する
		lastSuccessfulBuildTimeUtf8_ = NowTimeStringUtf8();
		lastFailureSummaryUtf8_.clear();
		// 型更新まで成功したので最後の正常版を更新する
		artifacts_.UpdateLastKnownGood(cycle_.currentShadowDir);
		artifacts_.PruneDirectories(artifacts_.StagingRoot());
		artifacts_.PruneDirectories(artifacts_.ShadowRoot());

		Logger::Output(LogType::Engine, spdlog::level::info,
			"ManagedScriptBuildService: 再読み込みに成功しました BuildID={} ReloadID={} 変更数={} "
			"Build時間={:.1f}ms Manifest時間={:.1f}ms Shadow時間={:.1f}ms Load時間={:.1f}ms 型数={} Fallback={}",
			cycle_.diagnostics.buildID, cycle_.diagnostics.reloadID, cycle_.diagnostics.changedSourceCount,
			cycle_.diagnostics.buildMs, cycle_.diagnostics.manifestMs, cycle_.diagnostics.shadowCopyMs, cycle_.diagnostics.loadMs,
			cycle_.diagnostics.scriptTypeCount, cycle_.diagnostics.fallbackUsed);

		FinishCycle(true);
		return;
	}

	// リロード失敗、最後の正常版から復旧を試みる
	SetState(State::ReloadFailed);
	Logger::Output(LogType::Engine, spdlog::level::err,
		"ManagedScriptBuildService: 再読み込みに失敗したためLastKnownGoodへ戻します BuildID={} ReloadID={}",
		cycle_.diagnostics.buildID, cycle_.diagnostics.reloadID);
	ApplyFallback();
}

void Engine::ManagedScriptBuildService::ApplyFallback() {

	SetState(State::FallbackLoading);
	cycle_.diagnostics.fallbackUsed = true;

	const std::filesystem::path lastKnownGoodDll = artifacts_.LastKnownGoodDirectory() / kAssemblyFileName;
	std::error_code existsError{};
	if (std::filesystem::exists(lastKnownGoodDll, existsError) && !existsError &&
		runtime_->LoadGameAssemblyFromPath(lastKnownGoodDll)) {

		SetState(State::FallbackSucceeded);
		Logger::Output(LogType::Engine, spdlog::level::warn,
			"ManagedScriptBuildService: LastKnownGood Assemblyで復旧しました path={}",
			ToUtf8Path(lastKnownGoodDll));
		// Editは復旧したが最新ビルドのリロードには失敗しているためPlay成功とはしない
		FinishCycle(false);
		return;
	}

	SetState(State::FallbackFailed);
	Logger::Output(LogType::Engine, spdlog::level::err,
		"ManagedScriptBuildService: LastKnownGoodへの復旧にも失敗したため修正までManaged Scriptを利用できません");
	FinishCycle(false);
}

void Engine::ManagedScriptBuildService::FinishCycle(bool succeeded) {

	// Play用ビルドの結果を確定する、このサイクルがforPlayの場合のみ
	if (cycle_.currentForPlay) {
		playBuildResult_ = succeeded ? PlayBuildResult::Succeeded : PlayBuildResult::Failed;
	}
	cycle_.currentForPlay = false;
	SetState(State::Idle);
}
