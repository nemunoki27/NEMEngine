#include "BufferUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// c++
#include <cstring>

//============================================================================
//	BufferUploadService classMethods
//============================================================================

void Engine::BufferUploadService::Init(ID3D12Device* device, ID3D12CommandQueue* graphicsQueue) {

	Finalize();

	device_ = device;
	graphicsQueue_ = graphicsQueue;

	// 転送はCopyBufferRegionとBarrierを1リストで扱いたいのでDIRECTキューにする
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HRESULT hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&uploadQueue_));
	assert(SUCCEEDED(hr));

	// アロケータReset待ちを避けるため複数コンテキストをリングで持つ
	contexts_.resize(kUploadContextCount);
	for (UploadFrameContext& context : contexts_) {

		hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context.allocator));
		assert(SUCCEEDED(hr));
		hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context.allocator.Get(), nullptr,
			IID_PPV_ARGS(&context.commandList));
		assert(SUCCEEDED(hr));
		// 作成直後は記録状態なので、BeginBatchでResetできるよう一旦閉じる
		context.commandList->Close();
		context.lastFenceValue = 0;
	}

	fence_ = nullptr;
	nextFenceValue_ = 1;
	lastSubmittedFenceValue_ = 0;
	hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));

	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent_ != nullptr);

	contextIndex_ = 0;
	currentContext_ = nullptr;
	batchOpened_ = false;
	hasCommands_ = false;
}

void Engine::BufferUploadService::Finalize() {

	// 未SubmitのBatchがあれば終了処理前に閉じる。stagingやCommandListを開いたまま残さない。
	if (batchOpened_ && currentContext_) {
		SubmitBatch();
	}

	// GPU使用中のstagingを巻き込まないよう、未完了分を待ってから解放する
	if (fence_) {
		WaitForAllUploads();
	}
	pendingBatches_.clear();
	for (UploadFrameContext& context : contexts_) {
		context.stagingResources.clear();
	}

	if (fenceEvent_) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}
	fence_.Reset();
	contexts_.clear();
	uploadQueue_.Reset();

	currentContext_ = nullptr;
	graphicsQueue_ = nullptr;
	device_ = nullptr;
	batchOpened_ = false;
	hasCommands_ = false;
	contextIndex_ = 0;
	nextFenceValue_ = 1;
	lastSubmittedFenceValue_ = 0;
}

void Engine::BufferUploadService::BeginBatch() {

	EnsureBatchOpened();
}

void Engine::BufferUploadService::EnsureBatchOpened() {

	if (batchOpened_) {
		return;
	}

	// 次に使うコンテキストを選び、そのコンテキストのGPU完了を待ってからResetする
	UploadFrameContext& context = contexts_[contextIndex_];
	WaitForFenceValue(context.lastFenceValue);

	HRESULT hr = context.allocator->Reset();
	assert(SUCCEEDED(hr));
	hr = context.commandList->Reset(context.allocator.Get(), nullptr);
	assert(SUCCEEDED(hr));

	currentContext_ = &context;
	batchOpened_ = true;
	hasCommands_ = false;

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	Logger::Output(LogType::Engine, "[BufferUpload][BeginBatch] context={}", contextIndex_);
#endif
}

void Engine::BufferUploadService::EnqueueBufferUpload(ID3D12Resource* destination,
	std::span<const std::byte> sourceData, D3D12_RESOURCE_STATES finalState) {

	if (!destination || sourceData.empty()) {
		return;
	}

	EnsureBatchOpened();

	// CPUデータをUPLOAD heap stagingへ書き込む
	ComPtr<ID3D12Resource> staging;
	DxUtils::CreateUploadBufferResource(device_, staging, sourceData.size_bytes());

	void* mapped = nullptr;
	HRESULT hr = staging->Map(0, nullptr, &mapped);
	assert(SUCCEEDED(hr));
	std::memcpy(mapped, sourceData.data(), sourceData.size_bytes());
	staging->Unmap(0, nullptr);

	// DEFAULT heap bufferはCreateCommittedResource時点ではCOMMONなので、コピー前にCOPY_DESTへ遷移する。
	D3D12_RESOURCE_BARRIER copyDestBarrier{};
	copyDestBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	copyDestBarrier.Transition.pResource = destination;
	copyDestBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
	copyDestBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
	copyDestBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	currentContext_->commandList->ResourceBarrier(1, &copyDestBarrier);

	// staging -> DEFAULT heapへコピー
	currentContext_->commandList->CopyBufferRegion(destination, 0, staging.Get(), 0, sourceData.size_bytes());

	// 必要なら最終状態へ遷移する(COPY_DESTのままにする場合はBarrierを積まない)
	if (finalState != D3D12_RESOURCE_STATE_COPY_DEST) {

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = destination;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = finalState;
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		currentContext_->commandList->ResourceBarrier(1, &barrier);
	}

	currentContext_->stagingResources.emplace_back(std::move(staging));
	hasCommands_ = true;

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	Logger::Output(LogType::Engine, "[BufferUpload][Enqueue] bytes={}", sourceData.size_bytes());
#endif
}

uint64_t Engine::BufferUploadService::SubmitBatch() {

	if (!batchOpened_ || !hasCommands_) {

		// 空Batchはコマンドリストを閉じて状態だけ戻す
		if (batchOpened_ && currentContext_) {
			currentContext_->commandList->Close();
		}
		batchOpened_ = false;
		hasCommands_ = false;
		return 0;
	}

	HRESULT hr = currentContext_->commandList->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* lists[] = { currentContext_->commandList.Get() };
	uploadQueue_->ExecuteCommandLists(1, lists);

	const uint64_t submittedFenceValue = nextFenceValue_++;
	uploadQueue_->Signal(fence_.Get(), submittedFenceValue);
	lastSubmittedFenceValue_ = submittedFenceValue;
	currentContext_->lastFenceValue = submittedFenceValue;

	// 描画キュー側でアップロード完了を待たせる(CPUは待たない)
	if (graphicsQueue_ && graphicsQueue_ != uploadQueue_.Get()) {
		graphicsQueue_->Wait(fence_.Get(), submittedFenceValue);
	}

	// stagingはFence完了まで保持する
	PendingBufferUploadBatch pending{};
	pending.fenceValue = submittedFenceValue;
	pending.stagingResources = std::move(currentContext_->stagingResources);
	currentContext_->stagingResources.clear();
	const size_t resourceCount = pending.stagingResources.size();
	pendingBatches_.emplace_back(std::move(pending));

#if defined(_DEBUG) || defined(_DEVELOPBUILD)
	Logger::Output(LogType::Engine, "[BufferUpload][Submit] fence={} resourceCount={}",
		submittedFenceValue, resourceCount);
#else
	(void)resourceCount;
#endif

	// 次回は別コンテキストを使う
	contextIndex_ = (contextIndex_ + 1) % kUploadContextCount;
	currentContext_ = nullptr;
	batchOpened_ = false;
	hasCommands_ = false;
	return submittedFenceValue;
}

void Engine::BufferUploadService::TickFinalize() {

	if (!fence_) {
		return;
	}

	const uint64_t completed = fence_->GetCompletedValue();
	while (!pendingBatches_.empty()) {

		if (completed < pendingBatches_.front().fenceValue) {
			break;
		}
#if defined(_DEBUG) || defined(_DEVELOPBUILD)
		Logger::Output(LogType::Engine, "[BufferUpload][Collect] fence={} releasedResourceCount={}",
			pendingBatches_.front().fenceValue, pendingBatches_.front().stagingResources.size());
#endif
		pendingBatches_.pop_front();
	}
}

void Engine::BufferUploadService::WaitForFenceValue(uint64_t fenceValue) {

	if (!fence_ || fenceValue == 0) {
		return;
	}
	if (fenceValue <= fence_->GetCompletedValue()) {
		return;
	}
	fence_->SetEventOnCompletion(fenceValue, fenceEvent_);
	WaitForSingleObject(fenceEvent_, INFINITE);
}

void Engine::BufferUploadService::WaitForAllUploads() {

	WaitForFenceValue(lastSubmittedFenceValue_);
}
