#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace Engine {

	//============================================================================
	//	ManagedProcessRunner class
	//	子プロセス（dotnet build 等）を非同期に起動し、stdout/stderr を pipe 経由で
	//	逐次取り込むための RAII ラッパー。Editor main thread を block しない。
	//============================================================================
	// process/thread/pipe ハンドルは RAII で管理し、Editor 終了時は子プロセスを安全に回収する。
	// _wsystem の同期実行を置き換えるために使う。
	class ManagedProcessRunner {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		ManagedProcessRunner() = default;
		~ManagedProcessRunner();

		ManagedProcessRunner(const ManagedProcessRunner&) = delete;
		ManagedProcessRunner& operator=(const ManagedProcessRunner&) = delete;

		// 子プロセスを起動する。commandLine は実行ファイルを含む完全なコマンドライン。
		// 子プロセス環境に DOTNET_CLI_UI_LANGUAGE=en を明示設定する。引数 quoting は呼び出し側責務。
		bool Start(const std::wstring& commandLine, const std::filesystem::path& workingDirectory);

		// 実行中か
		bool IsRunning() const { return process_ != nullptr; }

		// 出力を非ブロッキングに取り込み、行ごとに onLine へ渡す。
		// プロセス終了を検知したら残出力を flush して exit code を確定し true を返す。
		// まだ実行中なら false。低コストな polling 前提。
		bool Poll(const std::function<void(const std::string&)>& onLine);

		// Poll が true を返した後に有効な終了コード
		int32_t ExitCode() const { return exitCode_; }

		// 子プロセスを強制終了して回収する（Editor 終了時など）。複数回呼び出しても安全。
		void Terminate();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		// 全ハンドルを閉じる（process/thread/pipe）
		void CloseHandles();
		// pipe から読めるだけ読み、行へ分割して onLine へ渡す
		void DrainPipe(const std::function<void(const std::string&)>& onLine);

		//--------- variables ----------------------------------------------------

		void* process_ = nullptr;     // HANDLE
		void* thread_ = nullptr;      // HANDLE
		void* stdoutRead_ = nullptr;  // HANDLE（pipe read end）

		// 改行未満の残りバイト
		std::string pending_;
		int32_t exitCode_ = -1;
	};
} // Engine
