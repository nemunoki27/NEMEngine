#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedBuildProcess.h>
#include <Engine/Core/Scripting/Managed/ManagedSourceMonitor.h>
#include <Engine/Core/Scripting/Managed/ManagedBuildArtifacts.h>
#include <Engine/Core/Scripting/Managed/ManagedBuildCycle.h>
#include <Engine/Core/Scripting/Managed/Diagnostics/ManagedAlcStatus.h>

// c++
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace Engine {

	// front
	class ManagedScriptRuntime;

	//============================================================================
	//	ManagedScriptBuildService class
	//	EditモードのGameScripts reloadをEditorをblockせず安全に進める状態機械
	//============================================================================
	class ManagedScriptBuildService {
	public:
		//============================================================================
		//	public types
		//============================================================================
		// reload状態機械の状態
		enum class State : uint8_t {

			Idle,
			Debouncing,
			MetadataSyncing,
			Building,
			BuildSucceeded,
			BuildFailed,
			Staging,
			ReloadPending,
			Reloading,
			ReloadSucceeded,
			ReloadFailed,
			FallbackLoading,
			FallbackSucceeded,
			FallbackFailed,
		};

		// Play開始のためのbuild/reload要求の解決状態
		enum class PlayBuildResult : uint8_t {

			Pending,
			Succeeded,
			Failed,
		};

		// Editor panelが読むread-only snapshotでLogger文字列ではなく構造化状態をsource of truthとする、値はすべてcopy済みで毎UIフレームの安全な参照に使える
		struct Snapshot {

			State state = State::Idle;
			uint64_t buildID = 0;
			uint64_t reloadID = 0;
			bool hasPendingSourceChanges = false;
			bool reloadDeferredByPlayMode = false;
			bool hasUsableLastKnownGood = false;
			bool lastKnownGoodUpdateFailed = false;
			bool alcLeakSuspected = false;
			AlcUnloadStatus alcUnloadStatus = AlcUnloadStatus::Unknown;
			std::string activeAssemblyPath;
			std::string lastSuccessfulBuildTime;
			std::string lastFailureSummary;
		};

		ManagedScriptBuildService() = default;
		~ManagedScriptBuildService();

		ManagedScriptBuildService(const ManagedScriptBuildService&) = delete;
		ManagedScriptBuildService& operator=(const ManagedScriptBuildService&) = delete;

		// runtimeを関連付けて初期化し、source監視のbaselineとlast-known-goodを整える
		void Initialize(ManagedScriptRuntime* runtime);
		// 実行中の子プロセスを安全に終了して状態を破棄する、複数回呼び出しても安全
		void Shutdown();

		// 毎フレーム呼ぶ、playing中はreloadを適用せず変更検知のdirty記録だけ行う
		void Tick(bool playing);

		// Play開始のために最新のbuildやreloadを要求する、debounceを経ずに開始する
		void RequestPlayBuild();
		// Play要求の解決状態でPendingの間はPlay開始を保留する
		PlayBuildResult PollPlayBuild() const { return playBuildResult_; }
		// Play開始の保留要求が出ているか
		bool IsPlayBuildRequested() const { return playBuildRequested_; }

		State GetState() const { return state_; }
		// 未反映のsource変更つまりdirtyがあるか、Play中の保存表示などに使う
		bool HasPendingChanges() const { return dirty_; }

		//--------- Editor-facing service boundary -------------------------------
		// Editor panel向けのread-only snapshotを返す、構造化状態でlog再解析しない
		Snapshot GetSnapshot() const;

		// 明示rebuildを要求し状態機械を壊さずdirtyを立てるだけ、次の安全地点でbuildやreloadを行いPlay中は既存defer ruleを守る
		void RequestRebuild();
		// 直近の失敗後に再試行する、RequestRebuildと同義だが意図を明示する
		void RequestRetry();
		// metadata同期を要求する、rebuildサイクルの先頭で同期が走る
		void RequestMetadataSync();
		// 安全になった時点でreloadする要求でPlay中はStop後までdeferする
		void RequestReloadWhenSafe();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ManagedScriptRuntime* runtime_ = nullptr;

		ManagedBuildArtifacts artifacts_{ runtime_ };
		State state_ = State::Idle;
		ManagedBuildProcess process_;

		// source監視
		bool dirty_ = false;
		// Play中の変更を「Stop後に反映」と一度だけ通知したか
		bool playDirtyNotified_ = false;
		std::chrono::steady_clock::time_point lastChangeTime_{};
		ManagedSourceMonitor sourceMonitor_;

		// 現在のサイクル
		uint64_t buildCounter_ = 0;
		uint64_t reloadCounter_ = 0;
		ManagedBuildCycle cycle_;

		// Editor向けsnapshot用の状態でlog文字列をsource of truthにしない
		bool alcLeakSuspected_ = false; // HostBridge typed status 由来で LeakSuspected のとき true
		AlcUnloadStatus alcUnloadStatus_ = AlcUnloadStatus::Unknown;
		std::string lastSuccessfulBuildTimeUtf8_;
		std::string lastFailureSummaryUtf8_;

		// Play gate
		bool playBuildRequested_ = false;
		PlayBuildResult playBuildResult_ = PlayBuildResult::Succeeded;

		// 設定値、Editorの設定UIから変更できる拡張点
		std::chrono::milliseconds debounce_{ 400 };

		//--------- functions ----------------------------------------------------

		// 状態機械を1ステップ進める
		void AdvanceState(bool playing);

		// buildサイクルを開始する、まずscript metadata同期し成功でstaging buildへ進む
		bool StartBuild(bool forPlay);
		// metadata同期成功後に、組み立て済みのGameScripts staging buildを起動する
		bool StartGameScriptsBuild();
		// --no-dependencies buildに必要なNEM.ScriptCore.dllとNEM.ScriptCodeGen.dllを検証し不足ならfalseを返し不足パスと再ビルド手順をログへ出してprocessは起動しない
		bool VerifyBuildPrerequisites() const;
		// build完了処理でstaging検証してからshadow copyする
		void OnBuildFinished();
		// shadow copyからreloadを適用する
		void ApplyReload();
		// last-known-goodから復旧する
		void ApplyFallback();
		// サイクル終端でPlay gateとdirtyを解決しIdleへ戻す
		void FinishCycle(bool succeeded);

		// 状態遷移ログ
		void SetState(State next);

	};
} // Engine
