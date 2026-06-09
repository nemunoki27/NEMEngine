#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedProcessRunner.h>

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
	//	Edit モードの GameScripts reload を、Editor を block せず安全に進める状態機械。
	//============================================================================
	// source 監視（debounce 付き）→ staging へ非同期 dotnet build → shadow copy →
	// collectible ALC へ load → 成功で last-known-good 更新、という流れを管理する。
	// build/reload の適用（unload/load）は main thread の Tick 内でのみ行う。
	// Play 中は reload を適用せず、変更は dirty として記録するだけにする。
	class ManagedScriptBuildService {
	public:
		//============================================================================
		//	public types
		//============================================================================
		// reload 状態機械の状態
		enum class State : uint8_t {

			Idle,
			Debouncing,
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

		// Play 開始のための build/reload 要求の解決状態
		enum class PlayBuildResult : uint8_t {

			Pending,
			Succeeded,
			Failed,
		};

		//============================================================================
		//	public Methods
		//============================================================================
		ManagedScriptBuildService() = default;
		~ManagedScriptBuildService();

		ManagedScriptBuildService(const ManagedScriptBuildService&) = delete;
		ManagedScriptBuildService& operator=(const ManagedScriptBuildService&) = delete;

		// runtime を関連付けて初期化する。source 監視の baseline と last-known-good を整える
		void Initialize(ManagedScriptRuntime* runtime);
		// 実行中の子プロセスを安全に終了し、状態を破棄する。複数回呼び出しても安全
		void Shutdown();

		// 毎フレーム呼ぶ。playing 中は reload を適用せず、変更検知（dirty 記録）だけ行う
		void Tick(bool playing);

		// Play 開始のために最新の build/reload を要求する（debounce を経ずに開始）
		void RequestPlayBuild();
		// Play 要求の解決状態。Pending の間は Play 開始を保留する
		PlayBuildResult PollPlayBuild() const { return playBuildResult_; }
		// Play 開始の保留要求が出ているか
		bool IsPlayBuildRequested() const { return playBuildRequested_; }

		//--------- accessor -----------------------------------------------------

		State GetState() const { return state_; }
		// dirty（未反映の source 変更）があるか。Play 中の保存表示などに使う
		bool HasPendingChanges() const { return dirty_; }
	private:
		//============================================================================
		//	private types
		//============================================================================

		// source 監視用のスタンプ（更新時刻 + サイズ）
		struct SourceStamp {

			std::filesystem::file_time_type time{};
			std::uintmax_t size = 0;
		};

		// 1 回の reload サイクルの診断情報
		struct ReloadDiagnostics {

			uint64_t buildId = 0;
			uint64_t reloadId = 0;
			int32_t changedSourceCount = 0;
			int32_t buildExitCode = 0;
			int32_t scriptTypeCount = 0;
			double buildMs = 0.0;
			double stagingMs = 0.0;
			double shadowCopyMs = 0.0;
			double loadMs = 0.0;
			double manifestMs = 0.0;
			bool artifactValid = false;
			// Script Manifest の生成と検証に成功したか（load前に必須）
			bool manifestValid = false;
			bool fallbackUsed = false;
		};

		//============================================================================
		//	private Methods
		//============================================================================

		// source 変更を検知して dirty を更新する（throttle 付き polling）
		void PollSourceChanges();
		// 状態機械を 1 ステップ進める
		void AdvanceState(bool playing);

		// staging への dotnet build を開始する
		bool StartBuild(bool forPlay);
		// --no-dependencies build に必要な前提成果物(NEM.ScriptCore.dll / NEM.ScriptCodeGen.dll)を検証する。
		// 不足していれば false を返し、不足パスと再ビルド手順をログへ出す（process は起動しない）。
		bool VerifyBuildPrerequisites() const;
		// build 完了処理（staging 検証 → shadow copy）
		void OnBuildFinished();
		// shadow copy から reload を適用する
		void ApplyReload();
		// last-known-good から復旧する
		void ApplyFallback();
		// サイクル終端で Play gate と dirty を解決し Idle へ戻す
		void FinishCycle(bool succeeded);

		// staging/shadow ディレクトリのコピー・検証・掃除
		bool ValidateArtifacts(const std::filesystem::path& directory) const;
		bool CopyArtifacts(const std::filesystem::path& from, const std::filesystem::path& to) const;
		void PruneDirectories(const std::filesystem::path& parent) const;
		void UpdateLastKnownGood(const std::filesystem::path& shadowDirectory);
		void SeedLastKnownGood();

		// パス計算
		std::filesystem::path ManagedRoot() const;
		std::filesystem::path StagingRoot() const;
		std::filesystem::path ShadowRoot() const;
		std::filesystem::path LastKnownGoodDirectory() const;

		// 状態遷移ログ
		void SetState(State next);

		//--------- variables ----------------------------------------------------

		ManagedScriptRuntime* runtime_ = nullptr;

		State state_ = State::Idle;
		ManagedProcessRunner process_;

		// source 監視
		std::unordered_map<std::string, SourceStamp> sourceSnapshot_;
		bool hasSnapshot_ = false;
		bool dirty_ = false;
		// Play中の変更を「Stop後に反映」と一度だけ通知したか
		bool playDirtyNotified_ = false;
		std::chrono::steady_clock::time_point lastChangeTime_{};
		std::chrono::steady_clock::time_point nextScanTime_{};

		// 現在のサイクル
		uint64_t buildCounter_ = 0;
		uint64_t reloadCounter_ = 0;
		std::filesystem::path currentStagingDir_;
		std::filesystem::path currentShadowDir_;
		bool currentForPlay_ = false;
		std::chrono::steady_clock::time_point buildStartTime_{};
		ReloadDiagnostics diagnostics_{};

		// build failure 診断用に直近 build の情報を保持する（engine.log へ要約を残すため）。
		// stdout/stderr の全文は gameLogic.log 側にある。
		std::string lastBuildCommandUtf8_;
		std::filesystem::path lastBuildWorkingDir_;
		std::string firstErrorLine_;
		std::string lastErrorLine_;

		// Play gate
		bool playBuildRequested_ = false;
		PlayBuildResult playBuildResult_ = PlayBuildResult::Succeeded;

		// 設定値（将来 12 の設定UIから変更できる拡張点）
		std::chrono::milliseconds debounce_{ 400 };
		std::chrono::milliseconds scanInterval_{ 250 };
		int32_t maxRetainedDirectories_ = 3;
	};
} // Engine
