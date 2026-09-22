#pragma once

//============================================================================
//	include
//============================================================================
#include "FrameProfileHistory.h"

// c++
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Engine {

	//============================================================================
	//	FrameProfiler class
	//	フレームのCPUとGPUの計測結果を集計して公開する
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
			MeshBatchUpload, // Meshバッチ構築、差分更新、転送のCPU時間
			MeshBatchBuild,
			MeshBufferTransfer,
			MeshMaterialBuild,
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
		// 描画更新量とフレーム多重化の統計
		struct RenderingStatistics {

			// Mesh更新量は1フレーム中の全ViewとPassを合算する
			uint32_t meshRebuildCount = 0;
			uint32_t meshTransformUpdateCount = 0;
			uint32_t meshParameterUpdateCount = 0;
			uint32_t meshReuseCount = 0;
			uint64_t meshUpdatedInstances = 0;
			uint64_t meshTransferBytes = 0;

			// スキニング更新量
			uint32_t skinningDispatchCount = 0;
			uint32_t skinnedInstanceCount = 0;

			// Raytracing構築量
			uint32_t blasBuildCount = 0;
			uint32_t blasRefitCount = 0;
			uint32_t blasSkipCount = 0;
			uint32_t blasGeometryCount = 0;
			uint32_t tlasInstanceCount = 0;
			uint32_t tlasBuildCount = 0;
			uint32_t tlasRefitCount = 0;
			uint32_t tlasSkipCount = 0;

			// クラスターライト構築量
			uint32_t clusterCount = 0;
			uint32_t clusterLocalLightCount = 0;
			uint32_t clusterLightIndexCount = 0;
			uint32_t clusterOverflowCount = 0;

			// フレームコンテキスト状態
			uint32_t frameContextIndex = 0;
			uint32_t frameContextCount = 1;
			uint32_t queuedFrameCount = 0;
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
		// Mesh更新の理由と対象数を加算する
		void AddMeshUpdate(uint32_t rebuilds, uint32_t transforms, uint32_t parameters,
			uint32_t reused, uint64_t instances);
		void AddMeshTransferBytes(uint64_t bytes) { renderingStatistics_.meshTransferBytes += bytes; }
		// スキニングDispatchを加算する
		void AddSkinningDispatch(uint32_t instanceCount);
		// BLAS構築を加算する
		void AddBLASBuild(uint32_t geometryCount);
		// BLAS更新を加算する
		void AddBLASRefit(uint32_t geometryCount);
		// BLAS更新省略を加算する
		void AddBLASSkip(uint32_t geometryCount);
		// TLASインスタンス数を設定する
		void SetTLASInstanceCount(uint32_t instanceCount);
		// TLASの構築、更新、省略を加算する
		void AddTLASBuild();
		void AddTLASRefit();
		void AddTLASSkip();
		// クラスターライト統計を設定する
		void SetClusterStatistics(uint32_t clusterCount, uint32_t localLightCount,
			uint32_t lightIndexCount, uint32_t overflowCount);
		// フレームコンテキスト状態を設定する
		void SetFrameContextStatistics(uint32_t contextIndex,
			uint32_t contextCount, uint32_t queuedFrameCount);

		//--------- accessor -----------------------------------------------------

		float GetDeltaTimeSec() const { return deltaTimeSec_; }
		float GetFps() const { return deltaTimeSec_ > 0.0f ? 1.0f / deltaTimeSec_ : 0.0f; }
		float GetTotalTimeSec() const { return totalTimeSec_; }
		// カテゴリの平均処理時間(ms)
		float GetAverageMs(Category category) const;

		const std::vector<NamedTime>& GetGPUPassTimes() const { return gpuPassTimes_; }
		float FindGPUPassMs(std::string_view name) const;
		float GetGPUTotalMs() const;
		bool HasGPUData() const { return !gpuPassTimes_.empty(); }

		// ECSシステムごとの処理時間を処理順で保持
		const std::vector<NamedTime>& GetEcsSystemTimes() const { return ecsSystemTimes_; }
		bool HasEcsSystemData() const { return !ecsSystemTimes_.empty(); }
		// ECSのarchetype数
		uint32_t GetArchetypeCount() const { return archetypeCount_; }
		// ECSのチャンクメモリと構造変更統計
		const ECSStatistics& GetECSStatistics() const { return ecsStatistics_; }
		// 描画更新量とフレーム多重化の統計
		const RenderingStatistics& GetRenderingStatistics() const { return resolvedRenderingStatistics_; }

		//============================================================================
		//	ScopedSample
		//	RAIIでスコープ内の経過時間をカテゴリへ加算するヘルパ
		//============================================================================
		class ScopedSample {
		public:
			explicit ScopedSample(Category category) : category_(category), start_(std::chrono::high_resolution_clock::now()) {}
			~ScopedSample();

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

		//--------- variables ----------------------------------------------------

		float deltaTimeSec_ = 0.0f;
		float totalTimeSec_ = 0.0f;

		std::array<FrameProfileHistory, static_cast<size_t>(Category::Count)> measures_{};
		std::vector<NamedTime> gpuPassTimes_{};
		std::vector<NamedTime> ecsSystemTimes_{};
		// ECSのarchetype数の最新値
		uint32_t archetypeCount_ = 0;
		// ECSのチャンクメモリと構造変更統計
		ECSStatistics ecsStatistics_{};
		// 描画更新量とフレーム多重化の統計
		RenderingStatistics renderingStatistics_{};
		// UIに公開する前フレームの確定済み描画統計
		RenderingStatistics resolvedRenderingStatistics_{};

		// 最初のBeginFrameでは空の累積を確定させない
		bool firstFrame_ = true;
	};
} // Engine

