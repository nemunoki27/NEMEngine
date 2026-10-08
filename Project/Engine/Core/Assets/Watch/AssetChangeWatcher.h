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
	//	ディレクトリの変更通知を非同期に収集する
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
		bool IsRunning() const { return running_.load(); }
		const std::filesystem::path& GetDirectory() const { return directory_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::thread thread_;
		// 通知ファイル名の基準ディレクトリ
		std::filesystem::path directory_;
		// windows.hを公開しないハンドル所有
		void* directoryHandle_ = nullptr;
		// 停止通知のイベント
		void* stopEvent_ = nullptr;
		// 通知I/Oの完了イベント
		void* changeEvent_ = nullptr;

		// スレッド稼働フラグ
		std::atomic<bool> running_{ false };
		// 通知欠落時は個別パスに代えて全体を再走査する
		std::atomic<bool> rescanRequired_{ false };

		// 収集した変更ファイルの絶対パスを保護するミューテックス
		std::mutex mutex_;
		std::vector<std::filesystem::path> changedPaths_;

		//--------- functions ----------------------------------------------------

		// 監視スレッド本体
		void ThreadMain();
	};
}
