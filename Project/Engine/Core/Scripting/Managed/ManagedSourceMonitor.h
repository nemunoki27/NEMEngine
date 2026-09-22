#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Scripting/Managed/ManagedSourceWatcher.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	ManagedSourceMonitor class
	//	変更監視とソース基準を所有する
	//============================================================================
	class ManagedSourceMonitor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 走査基準を初期化する
		void Reset();
		// 監視を終了する
		void Stop();
		// 通知と補助走査で変更を検出する
		void Poll(const std::filesystem::path& projectPath, bool& dirty,
			std::chrono::steady_clock::time_point& lastChangeTime, int32_t& changedSourceCount);
		// ロード済みAssemblyより新しいソースを調べる
		bool IsNewerThan(const std::filesystem::path& assemblyPath) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct SourceStamp {

			std::filesystem::file_time_type time{};
			std::uintmax_t size = 0;
		};

		//--------- variables ----------------------------------------------------

		std::unordered_map<std::string, SourceStamp> sourceSnapshot_;
		bool hasSnapshot_ = false;
		std::chrono::steady_clock::time_point nextScanTime_{};
		ManagedSourceWatcher watcher_;
		std::filesystem::path watchedRoot_;
		std::chrono::milliseconds scanInterval_{ 5000 };
	};
}
