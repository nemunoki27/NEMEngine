#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <d3d12.h>
// c++
#include <cstddef>
#include <cstdint>
#include <deque>
#include <span>
#include <vector>

namespace Engine {

	//============================================================================
	//	BufferUploadService class
	//	DEFAULT heapの静的バッファへ初期データを転送するサービス。
	//	UPLOAD heap stagingの生成・コピー記録・Fence発行・staging解放を集約する。
	//	TextureUploadServiceとは独立。
	//============================================================================
	class BufferUploadService {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		BufferUploadService() = default;
		~BufferUploadService() = default;

		// アップロード専用のキュー/アロケータ/リスト/フェンスを作成する
		void Init(ID3D12Device* device, ID3D12CommandQueue* graphicsQueue);
		// GPU利用中のstagingを安全に解放してから破棄する
		void Finalize();

		// Batchを開始する。既に開いている場合は何もしない
		void BeginBatch();

		// DEFAULT heap destinationへの初期データ転送を記録する。
		// stagingを生成しCopyBufferRegionとBarrierを積む。実行はSubmitBatchで行う
		void EnqueueBufferUpload(ID3D12Resource* destination,
			std::span<const std::byte> sourceData, D3D12_RESOURCE_STATES finalState);

		// 記録済みBatchをキューへ提出し、Fence値を返す。コマンドが無ければ0を返す
		uint64_t SubmitBatch();

		// 完了したBatchのstagingを解放する(毎フレーム主スレッドで呼ぶ)
		void TickFinalize();

		//--------- accessor -----------------------------------------------------

		bool HasOpenBatch() const { return batchOpened_; }
		bool HasPendingUploads() const { return !pendingBatches_.empty(); }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		// Submit済みでGPU完了待ちのstaging一式
		struct PendingBufferUploadBatch {

			uint64_t fenceValue = 0;
			std::vector<ComPtr<ID3D12Resource>> stagingResources;
		};

		// アロケータ/リストを使い回すためのリングコンテキスト
		struct UploadFrameContext {

			ComPtr<ID3D12CommandAllocator> allocator;
			ComPtr<ID3D12GraphicsCommandList> commandList;
			// このコンテキストで最後に積んだBatchのFence値(Reset安全性の判定に使う)
			uint64_t lastFenceValue = 0;
			// Submitまで保持するstaging
			std::vector<ComPtr<ID3D12Resource>> stagingResources;
		};

		//--------- variables ----------------------------------------------------

		// アロケータReset待ちを避けるためのリング段数
		static constexpr uint32_t kUploadContextCount = 3;

		ID3D12Device* device_ = nullptr;
		ID3D12CommandQueue* graphicsQueue_ = nullptr;

		ComPtr<ID3D12CommandQueue> uploadQueue_;
		std::vector<UploadFrameContext> contexts_;
		uint32_t contextIndex_ = 0;
		UploadFrameContext* currentContext_ = nullptr;

		ComPtr<ID3D12Fence> fence_;
		uint64_t nextFenceValue_ = 1;
		uint64_t lastSubmittedFenceValue_ = 0;
		HANDLE fenceEvent_ = nullptr;

		bool batchOpened_ = false;
		bool hasCommands_ = false;

		std::deque<PendingBufferUploadBatch> pendingBatches_;

		//--------- functions ----------------------------------------------------

		// Batchが未開封なら開く
		void EnsureBatchOpened();
		// 指定Fence値の完了をCPUで待つ
		void WaitForFenceValue(uint64_t fenceValue);
		// 全Submit分の完了を待つ
		void WaitForAllUploads();
	};
} // Engine
