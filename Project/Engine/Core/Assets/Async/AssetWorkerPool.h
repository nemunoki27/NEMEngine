#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>
#include <functional>
#include <condition_variable>
#include <exception>
#include <stdexcept>

namespace Engine {

	//============================================================================
	//	AssetWorkerPool class
	//	汎用非同期ジョブワーカープール
	//============================================================================
	template <typename T>
	class AssetWorkerPool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// ワーカープールの状態
		struct WorkerPoolStats {

			uint32_t queuedCount = 0;
			uint32_t inFlightCount = 0;
			uint32_t threadCount = 0;
			bool stopping = false;
		};
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AssetWorkerPool() = default;
		~AssetWorkerPool();

		// 実行関数オブジェクト
		using ProcessFn = std::function<void(T&&, uint32_t workerIndex)>;

		// 開始
		void Start(uint32_t threadCount, ProcessFn process);
		// 待機中のジョブを処理し、全ワーカーの終了を待つ
		void Stop();

		// ジョブの追加
		bool Enqueue(T job);
		// 全ジョブの処理関数が戻るまで待つ
		void WaitIdle();

		//--------- accessor -----------------------------------------------------

		// 何もしていないか
		bool IsIdle() const;
		// ワーカープールの状態を取得
		WorkerPoolStats GetStats() const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// ワーカープールの状態を保護するミューテックス
		mutable std::mutex mutex_;
		// 開始と停止の手順を直列化する
		std::mutex lifecycleMutex_;
		// このスレッドが実行しているプール
		static inline thread_local const AssetWorkerPool* activeWorker_ = nullptr;
		// ワーカーの待機と通知のための条件変数
		std::condition_variable cv_;
		std::condition_variable idleCv_;

		// 処理待ちのジョブキュー
		std::deque<std::unique_ptr<T>> jobs_;
		// ワーカースレッドのコンテナ
		std::vector<std::thread> workers_;
		// ジョブ処理関数オブジェクト
		ProcessFn process_{};

		// 実行中のジョブ数
		uint32_t inFlight_ = 0;
		// 停止フラグ
		bool stopping_ = true;
		// 全ワーカーの起動後だけ投入を許可する
		bool accepting_ = false;
		// 呼出し側へ伝える最初の処理例外
		std::exception_ptr failure_;

		//--------- functions ----------------------------------------------------

		// ワーカースレッドのループ関数
		void WorkerLoop(uint32_t workerIndex);
		// 次のジョブを取得し、停止条件なら終了する
		bool TakeJob(std::unique_ptr<T>& job);
		// 自分のcallbackから完了待ちへ入らない
		void CheckControlThread() const;
		// lifecycleMutexの所有中にワーカーを終了する
		void StopWorkers();
		// 実行中件数を戻し、待機者へ完了を通知する
		void CompleteJob();
	};

	//============================================================================
	//	AssetWorkerPool templateMethods
	//============================================================================
	template<typename T>
	inline AssetWorkerPool<T>::~AssetWorkerPool() {

		Stop();
	}

	template<typename T>
	inline void AssetWorkerPool<T>::Start(uint32_t threadCount, ProcessFn process) {

		if (!process) {
			throw std::invalid_argument("AssetWorkerPool process is empty");
		}
		// 処理前に停止
		CheckControlThread();
		std::scoped_lock lifecycleLock(lifecycleMutex_);
		StopWorkers();
		{
			std::scoped_lock lock(mutex_);
			stopping_ = false;
			process_ = std::move(process);
			failure_ = nullptr;
		}

		// スレッド数は1以上
		threadCount = (std::max)(1u, threadCount);
		try {
			std::scoped_lock lock(mutex_);
			workers_.reserve(threadCount);
			for (uint32_t i = 0; i < threadCount; ++i) {
				workers_.emplace_back([this, i]() { WorkerLoop(i); });
			}
			accepting_ = true;
		} catch (...) {
			// 起動途中のスレッドも終了させてから失敗を返す
			StopWorkers();
			throw;
		}
	}

	template<typename T>
	inline void AssetWorkerPool<T>::Stop() {

		CheckControlThread();
		std::scoped_lock lifecycleLock(lifecycleMutex_);
		StopWorkers();
	}

	template<typename T>
	inline void AssetWorkerPool<T>::CheckControlThread() const {

		if (activeWorker_ == this) {
			throw std::logic_error("AssetWorkerPool cannot wait for its own worker");
		}
	}

	template<typename T>
	inline void AssetWorkerPool<T>::StopWorkers() {

		// すでに停止している場合は何もしない
		{
			std::scoped_lock lock(mutex_);
			accepting_ = false;
			if (workers_.empty()) {

				jobs_.clear();
				inFlight_ = 0;
				stopping_ = true;
				process_ = nullptr;
				idleCv_.notify_all();
				return;
			}
			stopping_ = true;
		}

		// ワーカーに停止を通知して全てのスレッドが終了するのを待つ
		cv_.notify_all();
		for (auto& worker : workers_) {
			if (worker.joinable()) {
				worker.join();
			}
		}

		// 状態をリセット
		{
			std::scoped_lock lock(mutex_);
			workers_.clear();
			jobs_.clear();
			inFlight_ = 0;
			process_ = nullptr;
			stopping_ = true;
		}
		idleCv_.notify_all();
	}

	template<typename T>
	inline bool AssetWorkerPool<T>::Enqueue(T job) {

		// ジョブをキューに追加してワーカーに通知
		{
			std::scoped_lock lock(mutex_);
			// 停止中や処理失敗後には新しい要求を受け付けない
			if (!accepting_ || stopping_ || failure_) {
				return false;
			}
			// 取得時は所有だけを移し、Tの移動例外をワーカーへ持ち込まない
			jobs_.push_back(std::make_unique<T>(std::move(job)));
		}
		cv_.notify_one();
		return true;
	}

	template<typename T>
	inline void AssetWorkerPool<T>::WaitIdle() {

		CheckControlThread();
		std::unique_lock lock(mutex_);
		idleCv_.wait(lock, [this]() {
			return jobs_.empty() && inFlight_ == 0;
			});
		// 完了数を戻してから呼出し側へ失敗を伝える
		if (failure_) {
			std::rethrow_exception(failure_);
		}
	}

	template<typename T>
	inline bool AssetWorkerPool<T>::IsIdle() const {

		std::scoped_lock lock(mutex_);
		return jobs_.empty() && inFlight_ == 0;
	}

	template<typename T>
	inline typename AssetWorkerPool<T>::WorkerPoolStats AssetWorkerPool<T>::GetStats() const {

		std::scoped_lock lock(mutex_);
		WorkerPoolStats stats{};
		stats.queuedCount = static_cast<uint32_t>(jobs_.size());
		stats.inFlightCount = inFlight_;
		stats.threadCount = static_cast<uint32_t>(workers_.size());
		stats.stopping = stopping_;
		return stats;
	}

	template<typename T>
	inline void AssetWorkerPool<T>::WorkerLoop(uint32_t workerIndex) {

		activeWorker_ = this;
		for (;;) {

			std::unique_ptr<T> job;
			if (!TakeJob(job)) {
				activeWorker_ = nullptr;
				return;
			}

			// ジョブを処理
			try {
				process_(std::move(*job), workerIndex);
			} catch (...) {
				// 例外でもスレッドと完了待ちを取り残さない
				std::scoped_lock lock(mutex_);
				if (!failure_) {
					failure_ = std::current_exception();
				}
			}
			// ジョブの所有も返してから完了を通知する
			job.reset();
			CompleteJob();
		}
	}

	template<typename T>
	inline bool AssetWorkerPool<T>::TakeJob(std::unique_ptr<T>& job) {

		std::unique_lock lock(mutex_);
		cv_.wait(lock, [this]() { return stopping_ || !jobs_.empty(); });
		if (stopping_ && jobs_.empty()) {
			return false;
		}
		job = std::move(jobs_.front());
		jobs_.pop_front();
		++inFlight_;
		return true;
	}

	template<typename T>
	inline void AssetWorkerPool<T>::CompleteJob() {

		std::scoped_lock lock(mutex_);
		if (0 < inFlight_) {
			--inFlight_;
		}
		if (jobs_.empty() && inFlight_ == 0) {
			idleCv_.notify_all();
		}
	}
} // Engine
