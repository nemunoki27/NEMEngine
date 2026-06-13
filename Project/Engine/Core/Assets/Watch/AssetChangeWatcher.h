#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <atomic>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

namespace Engine {

	//============================================================================
	//	AssetChangeWatcher class
	//	指定ディレクトリ配下の変更ファイルパスをReadDirectoryChangesWで非同期収集するクラス
	//	メインループに影響を出さないよう監視は専用スレッドで行い、結果はmainスレッドが取り出す
	//============================================================================
	class AssetChangeWatcher {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		AssetChangeWatcher() = default;
		~AssetChangeWatcher();

		AssetChangeWatcher(const AssetChangeWatcher&) = delete;
		AssetChangeWatcher& operator=(const AssetChangeWatcher&) = delete;

		// 監視を開始する、既に監視中なら一度停止してから張り直す
		bool Start(const std::filesystem::path& directory);
		// 監視を停止しスレッドを終了する、複数回呼んでも安全
		void Stop();

		//--------- accessor -----------------------------------------------------

		// 前回以降に変更があったファイルの絶対パスを取り出して内部バッファをクリアする
		void DrainChanges(std::vector<std::filesystem::path>& outPaths);
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 監視スレッド本体
		void ThreadMain();

		//--------- variables ----------------------------------------------------

		std::thread thread_;
		// 監視対象ディレクトリで変更通知のファイル名を絶対パス化するために保持する
		std::filesystem::path directory_;
		// 監視対象ディレクトリのハンドルでwindows.hに依存しないようvoid*で持つ、未確保はnullptr
		void* directoryHandle_ = nullptr;
		// スレッド停止を通知するイベントで同じくvoid*で持つ
		void* stopEvent_ = nullptr;

		// スレッド稼働フラグ
		std::atomic<bool> running_{ false };

		// 収集した変更ファイルの絶対パスを保護するミューテックス
		std::mutex mutex_;
		std::vector<std::filesystem::path> changedPaths_;
	};
} // Engine
