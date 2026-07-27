#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <array>
#include <chrono>
#include <cstdint>
#include <list>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	FrameProfiler class
	// フレームごとのCPU/GPU処理時間を集計し、エディタへ提供するシングルトン
	// 各フェーズの計測値を1フレーム分累積し、フレーム開始で確定して平均化する
	//============================================================================
	class FrameProfiler {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		// 計測カテゴリ
		enum class Category : uint32_t {

			Update,  // 更新全体でEngineApplication::Tick相当
			Ecs,     // ECSシステムの処理
			Script,  // C#スクリプトの処理
			Draw,    // 描画処理
			GPUWait, // GPU完了待ちでCPUがブロックした時間
			MeshBatchUpload, // Meshバッチデータ構築とGPU転送のCPUコストでstaticキャッシュMISSやSkinned/Billboardで走る
			Count
		};

		// 名前付きの処理時間(GPUパス/ ECSシステムなどで共用)
		struct NamedTime {

			std::string name;
			float milliseconds = 0.0f;
		};
		using GPUPassTime = NamedTime;

		// ECSのチャンクメモリと構造変更統計
		struct ECSStatistics {

			// エンティティ、アーキタイプ、チャンク数
			uint32_t entityCount = 0;
			uint32_t archetypeCount = 0;
			uint32_t chunkSlotCount = 0;
			uint32_t allocatedChunkCount = 0;
			// チャンクの確保量と使用量
			uint64_t allocatedChunkBytes = 0;
			uint64_t payloadBytes = 0;
			// 構造変更による移動量
			uint64_t structuralMigrationCount = 0;
			uint64_t relocatedComponentCount = 0;
			uint64_t relocatedComponentBytes = 0;
		};

		static FrameProfiler& GetInstance();

		// フレーム開始で前フレームの累積を確定し今フレームの累積をリセットする
		void BeginFrame(float deltaTimeSec, float totalTimeSec);
		// カテゴリへ計測時間(ms)を加算する
		void AddSample(Category category, float milliseconds);
		// GPU計測結果を各パスごとに設定する、空なら未計測扱い
		void SetGPUPassTimes(const std::vector<NamedTime>& passes);
		// ECSシステムごとの処理時間を処理順で設定する、空なら未計測扱い
		void SetEcsSystemTimes(const std::vector<NamedTime>& systems);
		// ECSのarchetype数を設定する、ForEachが走査するarchetypeの数
		void SetArchetypeCount(uint32_t count) { archetypeCount_ = count; }
		// ECSのチャンクメモリと構造変更統計を設定する
		void SetECSStatistics(const ECSStatistics& statistics) { ecsStatistics_ = statistics; }

		//--------- accessor -----------------------------------------------------

		float GetDeltaTimeSec() const { return deltaTimeSec_; }
		float GetFps() const { return deltaTimeSec_ > 0.0f ? 1.0f / deltaTimeSec_ : 0.0f; }
		float GetTotalTimeSec() const { return totalTimeSec_; }
		// カテゴリの平均処理時間(ms)
		float GetAverageMs(Category category) const;

		const std::vector<NamedTime>& GetGPUPassTimes() const { return gpuPassTimes_; }
		float GetGPUTotalMs() const;
		bool HasGPUData() const { return !gpuPassTimes_.empty(); }

		// ECSシステムごとの処理時間を処理順で保持
		const std::vector<NamedTime>& GetEcsSystemTimes() const { return ecsSystemTimes_; }
		bool HasEcsSystemData() const { return !ecsSystemTimes_.empty(); }
		// ECSのarchetype数
		uint32_t GetArchetypeCount() const { return archetypeCount_; }
		// ECSのチャンクメモリと構造変更統計
		const ECSStatistics& GetECSStatistics() const { return ecsStatistics_; }

		//============================================================================
		//	ScopedSample
		//	RAIIでスコープ内の経過時間をカテゴリへ加算するヘルパ
		//============================================================================
		class ScopedSample {
		public:
			explicit ScopedSample(Category category) :
				category_(category), start_(std::chrono::high_resolution_clock::now()) {
			}
			~ScopedSample() {

				const std::chrono::duration<float, std::milli> elapsed =
					std::chrono::high_resolution_clock::now() - start_;
				FrameProfiler::GetInstance().AddSample(category_, elapsed.count());
			}

			ScopedSample(const ScopedSample&) = delete;
			ScopedSample& operator=(const ScopedSample&) = delete;
		private:
			Category category_;
			std::chrono::high_resolution_clock::time_point start_;
		};
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// カテゴリごとの計測情報
		struct Measure {

			// 今フレームの累積時間(ms)
			float accumulator = 0.0f;
			// 確定済みフレームの計測履歴
			std::list<float> samples;
		};

		//--------- variables ----------------------------------------------------

		// 平均化するサンプル数
		static constexpr size_t kSmoothingSample = 8;

		float deltaTimeSec_ = 0.0f;
		float totalTimeSec_ = 0.0f;

		std::array<Measure, static_cast<size_t>(Category::Count)> measures_{};
		std::vector<NamedTime> gpuPassTimes_{};
		std::vector<NamedTime> ecsSystemTimes_{};
		// ECSのarchetype数の最新値
		uint32_t archetypeCount_ = 0;
		// ECSのチャンクメモリと構造変更統計
		ECSStatistics ecsStatistics_{};

		// 最初のBeginFrameでは空の累積を確定させない
		bool firstFrame_ = true;
	};
} // Engine

