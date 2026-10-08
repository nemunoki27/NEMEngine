#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedProcessRunner.h>

#include <cstdint>

namespace Engine {

	//============================================================================
	//	ManagedBuildProcess class
	//	構築プロセスと出力診断を所有する
	//============================================================================
	class ManagedBuildProcess {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 既存の実行器で構築を開始する
		bool Start(const std::wstring& command, const std::filesystem::path& directory);
		// 実行器を終了する
		void Terminate();
		// サイクルの診断を初期化する
		void ResetDiagnostics();
		// meta同期の出力を収集する
		bool PollMetadataSync(uint64_t buildID, uint64_t reloadID);
		// 構築の出力を収集する
		bool PollBuild(uint64_t buildID, uint64_t reloadID);

		//--------- accessor -----------------------------------------------------

		int32_t ExitCode() const;
		const std::string& FirstError() const { return firstErrorLine_; }
		const std::string& LastError() const { return lastErrorLine_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ManagedProcessRunner process_;
		std::string firstErrorLine_;
		std::string lastErrorLine_;
	};
}
