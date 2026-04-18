#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>
#include <condition_variable>

namespace Engine {

	//============================================================================
	//	AssetWorkerPool class
	//	汎用非同期ジョブワーカープール
	//============================================================================
	template <typename T>
	class AssetWorkerPool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// ワーカープールの状態
		struct WorkerPoolStats {

			uint32_t queuedCount = 0;
			uint32_t inFlightCount = 0;
			uint32_t threadCount = 0;
			bool stopping = false;
		};
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AssetWorkerPool() = default;
		~AssetWorkerPool();

		// 実行関数オブジェクト
		using ProcessFn = std::function<void(T&&, uint32_t workerIndex)>;

		// 開始
		void Start(uint32_t threadCount, ProcessFn process);
		// 停止
		void Stop();

		// ジョブの追加
		void Enqueue(T job);
		// 全てのジョブの完了を待機
		void WaitIdle();

		//--------- accessor -----------------------------------------------------

		// 何もしていないか
		bool IsIdle() const;
		// ワーカープールの状態を取得
		WorkerPoolStats GetStats() const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ワーカープールの状態を保護するミューテックス
		mutable std::mutex mutex_;
		// ワーカーの待機と通知のための条件変数
		std::condition_variable cv_;
		std::condition_variable idleCv_;

		// 処理待ちのジョブキュー
		std::deque<T> jobs_;
		// ワーカースレッドのコンテナ
		std::vector<std::thread> workers_;
		// ジョブ処理関数オブジェクト
		ProcessFn process_{};

		// 実行中のジョブ数
		uint32_t inFlight_ = 0;
		// 停止フラグ
		bool stopping_ = false;

		//--------- functions ----------------------------------------------------

		// ワーカースレッドのループ関数
		void WorkerLoop(uint32_t workerIndex);
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


		// 処理前に停止
		Stop();
		{
			std::scoped_lock lock(mutex_);
			stopping_ = false;
			process_ = std::move(process);
		}

		// スレッド数は1以上
		threadCount = (std::max)(1u, threadCount);
		workers_.reserve(threadCount);
		for (uint32_t i = 0; i < threadCount; ++i) {

			workers_.emplace_back([this, i]() { WorkerLoop(i); });
		}
	}

	template<typename T>
	inline void AssetWorkerPool<T>::Stop() {

		// すでに停止している場合は何もしない
		{
			std::scoped_lock lock(mutex_);
			if (workers_.empty()) {

				jobs_.clear();
				inFlight_ = 0;
				stopping_ = false;
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
		workers_.clear();

		// 状態をリセット
		{
			std::scoped_lock lock(mutex_);
			jobs_.clear();
			inFlight_ = 0;
			process_ = nullptr;
			stopping_ = false;
		}
		idleCv_.notify_all();
	}

	template<typename T>
	inline void AssetWorkerPool<T>::Enqueue(T job) {

		// ジョブをキューに追加してワーカーに通知
		{
			std::scoped_lock lock(mutex_);
			jobs_.push_back(std::move(job));
		}
		cv_.notify_one();
	}

	template<typename T>
	inline void AssetWorkerPool<T>::WaitIdle() {

		std::unique_lock lock(mutex_);
		idleCv_.wait(lock, [this]() {
			return jobs_.empty() && inFlight_ == 0;
			});
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

		for (;;) {

			T job{};
			// ジョブが利用可能になるまで待機
			{
				std::unique_lock lock(mutex_);
				cv_.wait(lock, [this]() {
					return stopping_ || !jobs_.empty();
					});
				if (stopping_ && jobs_.empty()) {
					return;
				}

				job = std::move(jobs_.front());
				jobs_.pop_front();
				++inFlight_;
			}

			// ジョブを処理
			process_(std::move(job), workerIndex);
			{
				std::scoped_lock lock(mutex_);
				if (0 < inFlight_) {
					--inFlight_;
				}
				if (jobs_.empty() && inFlight_ == 0) {
					idleCv_.notify_all();
				}
			}
		}
	}
} // Engine