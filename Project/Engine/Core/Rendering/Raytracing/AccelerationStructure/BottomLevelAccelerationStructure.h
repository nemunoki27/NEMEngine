#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Raytracing/AccelerationStructure/AccelerationStructureBuffer.h>
#include <Engine/Core/Rendering/Raytracing/RaytracingStructures.h>
#include <Engine/Core/Rendering/DxObject/Buffers/FrameUploadBufferAllocator.h>

// c++
#include <vector>

namespace Engine {
	class GraphicsCore;

	//============================================================================
	//	BottomLevelAccelerationStructure class
	//	低レベル加速化構造クラス
	//============================================================================
	class BottomLevelAccelerationStructure {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		BottomLevelAccelerationStructure() = default;
		~BottomLevelAccelerationStructure();
		BottomLevelAccelerationStructure(const BottomLevelAccelerationStructure&) = delete;
		BottomLevelAccelerationStructure& operator=(const BottomLevelAccelerationStructure&) = delete;
		BottomLevelAccelerationStructure(BottomLevelAccelerationStructure&& other) noexcept;
		BottomLevelAccelerationStructure& operator=(BottomLevelAccelerationStructure&& other) noexcept;

		// 回収窓口を接続する
		void SetRetirementQueue(GraphicsResourceRetirement& queue);

		// 所有資源をGPU完了まで退避する
		void Release();

		// BLASの構築
		void Build(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
			const RaytracingBLASInput& input);

		// BLASの更新
		void Update(ID3D12GraphicsCommandList6* commandList, const RaytracingBLASInput& input);
		// 既存バッファを再利用してBLASを完全再構築する
		void Rebuild(ID3D12GraphicsCommandList6* commandList, const RaytracingBLASInput& input);
		// GPU完了を確認して静的BLASの圧縮を進める
		bool ProcessCompaction(GraphicsCore& graphicsCore, uint64_t& copyBudget);
		uint64_t GetOriginalBytes() const { return originalBytes_; }
		bool HasPendingCompaction() const { return compaction_ != nullptr; }

		//--------- accessor -----------------------------------------------------

		// ASが構築されているか
		bool IsBuilt() const { return result_.GetResource() != nullptr; }

		ID3D12Resource* GetResource() const { return result_.GetResource(); }
		UINT64 GetRetainedScratchBytes() const { return scratch_.GetResource() ? scratchSize_ : 0; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		struct CompactionState {

			AccelerationStructureBuffer size;
			AccelerationStructureBuffer readback;
			AccelerationStructureBuffer candidate;
			GraphicsSubmissionTicket build;
			GraphicsSubmissionTicket copy;
			uint64_t compactedBytes = 0;
		};
		//--------- variables ----------------------------------------------------

		std::unique_ptr<CompactionState> compaction_;
		uint64_t originalBytes_ = 0;

		// 加速化構造バッファ
		AccelerationStructureBuffer scratch_;
		AccelerationStructureBuffer result_;
		// ジオメトリローカル行列のアップロードバッファ
		FrameUploadBufferAllocator geometryTransformBuffer_{ 256 };
		GraphicsResourceRetirement* retirementQueue_ = nullptr;

		// ジオメトリ記述
		std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs_{};
		// ビルド記述
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs_{};
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc_{};

		ID3D12Device8* device_ = nullptr;
		// 更新を許可するか
		bool allowUpdate_ = false;
		UINT64 scratchSize_ = 0;
		// 更新時に変えられないジオメトリレイアウト
		uint64_t layoutHash_ = 0;

		//--------- functions ----------------------------------------------------

		// 未公開の候補へAS構築を記録する
		void BuildResources(ID3D12Device8* device, ID3D12GraphicsCommandList6* commandList,
			const RaytracingBLASInput& input);

		// 所有と構築状態を一組で交換する
		void Swap(BottomLevelAccelerationStructure& other) noexcept;

		// ジオメトリ記述とローカル行列の設定
		void FillGeometryDescs(const RaytracingBLASInput& input);
		// 静的ASの作業領域をGPU完了後に回収する
		void RetireStaticScratch();
		// 構築後サイズを非同期で読み戻す
		void RecordCompactionSize(ID3D12GraphicsCommandList6* commandList, const GraphicsSubmissionTicket& submission);
		// 圧縮候補と読み戻し資源をGPU完了まで回収へ渡す
		void ReleaseCompaction();
		// 更新可否を判定するレイアウトHashを計算
		static uint64_t ComputeLayoutHash(const RaytracingBLASInput& input);
	};
} // Engine
