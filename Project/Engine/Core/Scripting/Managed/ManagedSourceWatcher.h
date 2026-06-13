#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <atomic>
#include <filesystem>
#include <thread>

namespace Engine {

	//============================================================================
	//	ManagedSourceWatcher class
	// 指定ディレクトリ配下の変更をReadDirectoryChangesWで監視しフラグで通知するクラス
	//============================================================================
	class ManagedSourceWatcher {
	public:
		//========================================================================
		//	public Methods
		//========================================================================
		ManagedSourceWatcher() = default;
		~ManagedSourceWatcher();

		ManagedSourceWatcher(const ManagedSourceWatcher&) = delete;
		ManagedSourceWatcher& operator=(const ManagedSourceWatcher&) = delete;

		// 監視を開始する、既に監視中なら一度停止してから張り直す
		bool Start(const std::filesystem::path& directory);
		// 監視を停止しスレッドを終了する、複数回呼んでも安全
		void Stop();

		//--------- accessor -----------------------------------------------------

		// 前回の確認以降に変更があったかを取り出してフラグをクリアする
		bool ConsumeChanged() { return changed_.exchange(false); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 監視スレッド本体
		void ThreadMain();

		//--------- variables ----------------------------------------------------

		std::thread thread_;
		// 監視対象ディレクトリのハンドルでwindows.hに依存しないようvoid*で持つ、未確保はnullptr
		void* directoryHandle_ = nullptr;
		// スレッド停止を通知するイベントで同じくvoid*で持つ
		void* stopEvent_ = nullptr;

		// 変更検知フラグ
		std::atomic<bool> changed_{ false };
		// スレッド稼働フラグ
		std::atomic<bool> running_{ false };
	};
} // Engine
