#pragma once

//============================================================================
//	include
//============================================================================
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Engine {

	//============================================================================
	//	ManagedBuildCycle structure
	//	構築中の入力と成果物と診断
	//============================================================================
	struct ManagedBuildCycle {

		struct ReloadDiagnostics {

			uint64_t buildID = 0;
			uint64_t reloadID = 0;
			int32_t changedSourceCount = 0;
			int32_t buildExitCode = 0;
			int32_t scriptTypeCount = 0;
			double buildMs = 0.0;
			double shadowCopyMs = 0.0;
			double loadMs = 0.0;
			double manifestMs = 0.0;
			bool artifactValid = false;
			// Script Manifestの生成と検証に成功したかでload前に必須
			bool manifestValid = false;
			bool fallbackUsed = false;
		};

		std::filesystem::path currentStagingDir;
		std::filesystem::path currentShadowDir;
		bool currentForPlay = false;
		std::chrono::steady_clock::time_point buildStartTime{};
		ReloadDiagnostics diagnostics{};

		// build failure診断用に直近buildの情報を保持しengine.logへ要約を残す、stdoutとstderrの全文はgameLogic.log側にある
		std::string lastBuildCommandUtf8;
		std::filesystem::path lastBuildWorkingDir;
		// metadata同期成功後に起動するGameScripts staging buildコマンド
		std::wstring pendingBuildCommand;

	};
}
