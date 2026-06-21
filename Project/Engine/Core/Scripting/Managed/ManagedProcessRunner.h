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
	//	子プロセスを非同期に起動しstdoutとstderrをpipe経由で逐次取り込むRAIIラッパー
	//============================================================================
	class ManagedProcessRunner {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ManagedProcessRunner() = default;
		~ManagedProcessRunner();

		ManagedProcessRunner(const ManagedProcessRunner&) = delete;
		ManagedProcessRunner& operator=(const ManagedProcessRunner&) = delete;

		// 子プロセスを起動する、commandLineは実行ファイルを含む完全なコマンドラインで環境にDOTNET_CLI_UI_LANGUAGE=enを明示設定する、引数quotingは呼び出し側責務
		bool Start(const std::wstring& commandLine, const std::filesystem::path& workingDirectory);

		// 実行中か
		bool IsRunning() const { return process_ != nullptr; }

		// 出力を非ブロッキングに取り込み行ごとにonLineへ渡す、終了検知で残出力をflushしexit codeを確定してtrueを返し実行中ならfalseの低コストpolling前提
		bool Poll(const std::function<void(const std::string&)>& onLine);

		// Pollがtrueを返した後に有効な終了コード
		int32_t ExitCode() const { return exitCode_; }

		// 子プロセスを強制終了して回収する、Editor終了時など複数回呼び出しても安全
		void Terminate();
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		// processとthreadとpipeの全ハンドルを閉じる
		void CloseHandles();
		// pipeから読めるだけ読み、行へ分割してonLineへ渡す
		void DrainPipe(const std::function<void(const std::string&)>& onLine);

		//--------- variables ----------------------------------------------------

		void* process_ = nullptr;     // HANDLE
		void* thread_ = nullptr;      // HANDLE
		void* stdoutRead_ = nullptr;  // HANDLE pipe read end

		// 改行未満の残りバイト
		std::string pending_;
		int32_t exitCode_ = -1;
	};
} // Engine
