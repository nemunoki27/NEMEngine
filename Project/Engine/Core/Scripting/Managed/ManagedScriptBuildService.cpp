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

	// GameScriptsのアセンブリ/付随ファイル名
	constexpr const wchar_t* kAssemblyFileName = L"GameScripts.dll";
	// マニフェストは安定スクリプト型GUIDの一覧でロード前に検証する成果物
	constexpr const wchar_t* kManifestFileName = L"GameScripts.scriptmanifest.json";

	// 状態名でログ用
	const char* StateName(Engine::ManagedScriptBuildService::State state) {
		using State = Engine::ManagedScriptBuildService::State;
		switch (state) {
		case State::Idle: return "待機";
		case State::Debouncing: return "変更待機";
		case State::MetadataSyncing: return "Metadata同期中";
		case State::Building: return "ビルド中";
		case State::BuildSucceeded: return "ビルド成功";
		case State::BuildFailed: return "ビルド失敗";
		case State::Staging: return "Staging中";
		case State::ReloadPending: return "再読み込み待機";
		case State::Reloading: return "再読み込み中";
		case State::ReloadSucceeded: return "再読み込み成功";
		case State::ReloadFailed: return "再読み込み失敗";
		case State::FallbackLoading: return "Fallback読み込み中";
		case State::FallbackSucceeded: return "Fallback成功";
		case State::FallbackFailed: return "Fallback失敗";
		}
		return "不明";
	}

}

//============================================================================
//	ManagedScriptBuildService classMethods
//============================================================================
Engine::ManagedScriptBuildService::~ManagedScriptBuildService() {
	Shutdown();
}

void Engine::ManagedScriptBuildService::Initialize(ManagedScriptRuntime* runtime) {

	runtime_ = runtime;
	state_ = State::Idle;
	dirty_ = false;
	sourceMonitor_.Reset();
	playBuildRequested_ = false;
	playBuildResult_ = PlayBuildResult::Succeeded;

	// 起動時にソースの基準を取得する、初回は変更扱いにしない
	sourceMonitor_.Poll(runtime_->GameScriptProjectPath(), dirty_, lastChangeTime_, cycle_.diagnostics.changedSourceCount);
	dirty_ = false;

	// 現行Assemblyより新しい正常版があれば優先し、なければ現行Assemblyを復旧用へ保存する
	if (!artifacts_.RestoreLastKnownGoodOnStartup() && runtime_ && runtime_->HasLoadedGameAssembly()) {
		artifacts_.SeedLastKnownGood();
	}

	// ただしロード済みアセンブリよりソースが新しければEdit中に再ビルドさせる
	// エディタ起動前に編集した.csを、Playを押さずにインスペクターへ反映するため
	if (runtime_ && sourceMonitor_.IsNewerThan(runtime_->ActiveAssemblyPath())) {

		dirty_ = true;
		// すぐにビルドへ進ませるためデバウンス済み扱いにする
		lastChangeTime_ = std::chrono::steady_clock::now() - debounce_;
	}
}

void Engine::ManagedScriptBuildService::Shutdown() {

	process_.Terminate();
	sourceMonitor_.Stop();
	state_ = State::Idle;
	runtime_ = nullptr;
}

void Engine::ManagedScriptBuildService::Tick(bool playing) {

	if (!runtime_ || !runtime_->IsInitialized()) {
		return;
	}

	// 変更検知でPlay中もdirtyは記録するがリロードはしない
	sourceMonitor_.Poll(runtime_->GameScriptProjectPath(), dirty_, lastChangeTime_, cycle_.diagnostics.changedSourceCount);

	// Play中に変更があったら「Stop後に反映」を一度だけ通知する
	if (playing) {
		if (dirty_ && !playDirtyNotified_) {

			Logger::Output(LogType::GameLogic, spdlog::level::info,
				"GameScripts: Play中のC#変更を検出しました Stop後にビルドと再読み込みを行います");
			playDirtyNotified_ = true;
		}
	} else {
		playDirtyNotified_ = false;
	}

	// 状態機械を進める
	AdvanceState(playing);
}

void Engine::ManagedScriptBuildService::RequestPlayBuild() {

	playBuildRequested_ = true;
	playBuildResult_ = PlayBuildResult::Pending;

	// ビルド対象が無ければ即成功でスクリプト無しでもPlay可能
	if (!runtime_ || runtime_->GameScriptProjectPath().empty()) {
		playBuildResult_ = PlayBuildResult::Succeeded;
	}
}

void Engine::ManagedScriptBuildService::AdvanceState(bool playing) {

	switch (state_) {
	case State::Idle:
	{
		// Play用の強制ビルドを最優先で開始する
		if (!playing && playBuildRequested_ && playBuildResult_ == PlayBuildResult::Pending) {
			StartBuild(true);
			return;
		}
		// 通常のEdit変更はデバウンスへ
		if (!playing && dirty_) {
			SetState(State::Debouncing);
		}
		return;
	}
	case State::Debouncing:
	{
		// Playへ入ったら一旦保留し、変更はdirtyのままでStop後に再開する
		if (playing || !dirty_) {
			SetState(State::Idle);
			return;
		}
		const auto now = std::chrono::steady_clock::now();
		if (now - lastChangeTime_ >= debounce_) {
			StartBuild(false);
		}
		return;
	}
	case State::MetadataSyncing:
	{
		const bool finished = process_.PollMetadataSync(cycle_.diagnostics.buildID, cycle_.diagnostics.reloadID);
		if (finished) {

			const int32_t exitCode = process_.ExitCode();
			if (exitCode == 0) {
				// 採番成功でステージングビルドへ
				StartGameScriptsBuild();
			} else {
				// exit 2は手動解決が必要な曖昧リネームでexit 1は失敗、いずれも現行DLLを維持して保留する
				Logger::Output(LogType::Engine, spdlog::level::err,
					"ManagedScriptBuildService: Script Metadata同期に失敗したため現在のAssemblyを維持します "
					"終了Code={} 最初='{}' 最後='{}' 詳細はgameLogic.logを確認してください",
					exitCode, process_.FirstError().empty() ? "なし" : process_.FirstError(),
					process_.LastError().empty() ? "なし" : process_.LastError());
				if (cycle_.currentForPlay) {
					playBuildResult_ = PlayBuildResult::Failed;
				}
				SetState(State::BuildFailed);
				FinishCycle(false);
			}
		}
		return;
	}
	case State::Building:
	{
		const bool finished = process_.PollBuild(cycle_.diagnostics.buildID, cycle_.diagnostics.reloadID);
		if (finished) {
			OnBuildFinished();
		}
		return;
	}
	case State::ReloadPending:
	{
		// リロードの適用はメインスレッドかつEditモードでのみ行う
		if (playing) {
			return;
		}
		ApplyReload();
		return;
	}
	default:
		// 他の状態はOnBuildFinished/ApplyReload/ApplyFallback内で同期的に遷移済み
		return;
	}
}

void Engine::ManagedScriptBuildService::SetState(State next) {

	if (state_ == next) {
		return;
	}
	Logger::Output(LogType::Engine, spdlog::level::info,
		"ManagedScriptBuildService: 状態 {} -> {}", StateName(state_), StateName(next));
	state_ = next;

	// 失敗系へ遷移したらスナップショット用の失敗要約を構造化状態として記録する、ログ再解析しない
	if (next == State::BuildFailed || next == State::ReloadFailed || next == State::FallbackFailed) {
		lastFailureSummaryUtf8_ = process_.FirstError().empty()
			? std::string(StateName(next))
			: process_.FirstError();
	}
}

Engine::ManagedScriptBuildService::Snapshot Engine::ManagedScriptBuildService::GetSnapshot() const {

	Snapshot snapshot{};
	snapshot.state = state_;
	snapshot.buildID = cycle_.diagnostics.buildID;
	snapshot.reloadID = cycle_.diagnostics.reloadID;
	snapshot.hasPendingSourceChanges = dirty_;
	snapshot.reloadDeferredByPlayMode = playDirtyNotified_;
	std::error_code ec{};
	snapshot.hasUsableLastKnownGood =
		std::filesystem::exists(artifacts_.LastKnownGoodDirectory() / kAssemblyFileName, ec) && !ec;
	snapshot.lastKnownGoodUpdateFailed = artifacts_.HasUpdateFailed();
	snapshot.alcLeakSuspected = alcLeakSuspected_;
	snapshot.alcUnloadStatus = alcUnloadStatus_;
	snapshot.activeAssemblyPath = runtime_ ? ToUtf8Path(runtime_->ActiveAssemblyPath()) : std::string{};
	snapshot.lastSuccessfulBuildTime = lastSuccessfulBuildTimeUtf8_;
	snapshot.lastFailureSummary = lastFailureSummaryUtf8_;
	return snapshot;
}

void Engine::ManagedScriptBuildService::RequestRebuild() {

	// 次の安全地点でビルドやリロードを開始させる、状態機械は触らずdirtyを立てるだけでPlay中は保留される
	dirty_ = true;
	lastChangeTime_ = std::chrono::steady_clock::now() - debounce_;
}

void Engine::ManagedScriptBuildService::RequestRetry() {

	RequestRebuild();
}

void Engine::ManagedScriptBuildService::RequestMetadataSync() {

	// ビルドサイクルの先頭でメタデータ同期が走るため再ビルド要求と同じ経路で良い
	RequestRebuild();
}

void Engine::ManagedScriptBuildService::RequestReloadWhenSafe() {

	// 安全になった時点でリロードし、Play中は既存の保留規則つまりStop後反映に従う
	RequestRebuild();
}
