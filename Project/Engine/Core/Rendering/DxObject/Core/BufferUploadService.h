#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>
#include <Engine/Core/Rendering/DxObject/Buffers/DxMappedUploadBuffer.h>

// c++
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <deque>
#include <span>
#include <memory>
#include <vector>

// directX
#include <d3d12.h>

namespace Engine {
	// 所有付き転送の記録数と提出Fenceを保持する
	class BufferUploadCompletion {
	private:
		friend class BufferUploadService;
		std::shared_ptr<const void> source;
		uint64_t fence = 0;
		uint32_t pending = 0;
		bool canceled = false;
	};
	using BufferUploadTicket = std::shared_ptr<BufferUploadCompletion>;

	// 転送の累計と再利用中の容量
	struct BufferUploadStatistics {

		uint64_t uploadedBytes = 0;
		uint64_t uploadCount = 0;
		uint64_t stagingCreationCount = 0;
		uint64_t pooledBytes = 0;
		uint64_t largePooledBytes = 0;
		uint64_t pendingBytes = 0;
		uint64_t stagingReuseCount = 0;
	};

	//============================================================================
	//	BufferUploadService class
	// DEFAULT heapの静的バッファへ初期データを転送するサービス
	// UPLOAD heap stagingの生成・コピー記録・Fence発行・staging解放を集約する
	// TextureUploadServiceとは独立
	//============================================================================
	class BufferUploadService {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		BufferUploadService() = default;
		~BufferUploadService();
		BufferUploadService(const BufferUploadService&) = delete;
		BufferUploadService& operator=(const BufferUploadService&) = delete;

		// アップロード専用のキュー/アロケータ/リスト/フェンスを作成する
		void Init(GraphicsResourceRetirement& retirement, ID3D12Device* device, ID3D12CommandQueue* graphicsQueue);
		// GPU利用中のstagingを安全に解放してから破棄する
		void Finalize();

		// Batchを開始し既に開いている場合は何もしない
		void BeginBatch();
		// 元データを共有し、大容量転送をframeごとに分割する
		BufferUploadTicket BeginOwnedUploads(std::shared_ptr<const void> source);
		void FinishOwnedUploads();
		void CancelOwnedUploads(const BufferUploadTicket& ticket);
		bool IsComplete(const BufferUploadTicket& ticket) const;
		void PumpOwnedUploads(bool unlimited = false);

		// DEFAULT heap destinationへの初期データ転送を記録する
		// stagingへ書き込みCopyBufferRegionとBarrierを積む、実行はSubmitBatchで行う
		void EnqueueBufferUpload(ID3D12Resource* destination,
			std::span<const std::byte> sourceData, D3D12_RESOURCE_STATES finalState);
		// 既存DEFAULT heapの指定範囲だけを更新し、前後の状態を復元する
		void EnqueueBufferUpload(ID3D12Resource* destination,
			size_t destinationOffset, std::span<const std::byte> sourceData,
			D3D12_RESOURCE_STATES currentState,
			D3D12_RESOURCE_STATES finalState);

		// 記録済みBatchをキューへ提出しFence値を返す、コマンドが無ければ0を返す
		uint64_t SubmitBatch(bool waitForGraphics = true);

		// 完了したBatchのstagingを解放する(毎フレーム主スレッドで呼ぶ)
		void TickFinalize();
		// 未提出のBatchを送信し全GPU転送の完了を待つ
		void FlushAndWait();

		//--------- accessor -----------------------------------------------------

		GraphicsResourceRetirement& GetResourceRetirement() const;
		bool HasOpenBatch() const { return batchOpened_; }
		bool HasPendingUploads() const { return !pendingBatches_.empty() || !ownedUploads_.empty(); }
		const BufferUploadStatistics& GetStatistics() const { return statistics_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// Submit済みでGPU完了待ちの転送元と転送先
		struct PendingBufferUploadBatch {

			uint64_t fenceValue = 0;
			std::vector<ComPtr<ID3D12Resource>> retainedResources;
		};

		// 同じBatch内で書き込み範囲を分ける転送元
		struct UploadPage {

			DxMappedUploadBuffer buffer;
			size_t offset = 0;
		};

		// アロケータ/リストを使い回すためのリングコンテキスト
		struct UploadFrameContext {

			ComPtr<ID3D12CommandAllocator> allocator;
			ComPtr<ID3D12GraphicsCommandList> commandList;
			// このコンテキストで最後に積んだBatchのFence値(Reset安全性の判定に使う)
			uint64_t lastFenceValue = 0;
			// Submitまで保持する転送元と転送先
			std::vector<ComPtr<ID3D12Resource>> retainedResources;
			std::vector<UploadPage> pages;
			std::chrono::steady_clock::time_point lastUsed{};
		};

		struct OwnedUpload {

			ComPtr<ID3D12Resource> destination;
			std::span<const std::byte> source;
			size_t destinationOffset = 0;
			size_t offset = 0;
			D3D12_RESOURCE_STATES before = D3D12_RESOURCE_STATE_COMMON;
			D3D12_RESOURCE_STATES after = D3D12_RESOURCE_STATE_COMMON;
			BufferUploadTicket ticket;
		};
		struct LargeUploadPage {

			DxMappedUploadBuffer buffer;
			uint64_t fence = 0;
			std::chrono::steady_clock::time_point lastUsed{};
		};
		//--------- variables ----------------------------------------------------

		std::deque<OwnedUpload> ownedUploads_;
		std::vector<LargeUploadPage> largePages_;
		BufferUploadTicket activeUpload_;
		uint64_t largeUploadFrame_ = UINT64_MAX;
		size_t largeUploadedThisFrame_ = 0;

		// アロケータReset待ちを避けるためのリング段数
		static constexpr uint32_t kUploadContextCount = 3;
		static constexpr size_t kUploadPageSize = 1024 * 1024;
		BufferUploadStatistics statistics_{};

		ID3D12Device* device_ = nullptr;
		GraphicsResourceRetirement* retirement_ = nullptr;
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
		// 提出済みBatchへFenceを対応付ける
		uint64_t SignalSubmittedBatch();
		// 全Submit分の完了を待つ
		void WaitForAllUploads();
		// 完了済みの未使用ページを回収する
		void CollectUnusedPages(uint64_t completed);
	};
} // Engine
