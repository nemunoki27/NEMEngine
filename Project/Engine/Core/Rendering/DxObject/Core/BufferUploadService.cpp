#include "BufferUploadService.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <cstring>
#include <string>

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
	uploadQueue_->SetName(L"BufferUploadQueue");

	// アロケータReset待ちを避けるため複数コンテキストをリングで持つ
	contexts_.resize(kUploadContextCount);
	for (size_t i = 0; i < contexts_.size(); ++i) {
		UploadFrameContext& context = contexts_[i];
		hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context.allocator));
		assert(SUCCEEDED(hr));
		context.allocator->SetName((L"BufferUploadCommandAllocator[" + std::to_wstring(i) + L"]").c_str());

		hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context.allocator.Get(), nullptr,
			IID_PPV_ARGS(&context.commandList));
		assert(SUCCEEDED(hr));
		context.commandList->SetName((L"BufferUploadCommandList[" + std::to_wstring(i) + L"]").c_str());

		// 作成直後は記録状態なので、BeginBatchでResetできるよう一旦閉じる
		context.commandList->Close();
		context.lastFenceValue = 0;
	}

	fence_ = nullptr;
	nextFenceValue_ = 1;
	lastSubmittedFenceValue_ = 0;
	hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));
	fence_->SetName(L"BufferUploadFence");

	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	assert(fenceEvent_ != nullptr);

	contextIndex_ = 0;
	currentContext_ = nullptr;
	batchOpened_ = false;
	hasCommands_ = false;
}

void Engine::BufferUploadService::Finalize() {

	// 未SubmitのBatchがあれば終了処理前に閉じ、stagingやCommandListを開いたまま残さない
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
}

void Engine::BufferUploadService::EnqueueBufferUpload(ID3D12Resource* destination,
	std::span<const std::byte> sourceData, D3D12_RESOURCE_STATES finalState) {

	EnqueueBufferUpload(destination, 0, sourceData,
		D3D12_RESOURCE_STATE_COMMON, finalState);
}

void Engine::BufferUploadService::EnqueueBufferUpload(
	ID3D12Resource* destination, size_t destinationOffset,
	std::span<const std::byte> sourceData,
	D3D12_RESOURCE_STATES currentState,
	D3D12_RESOURCE_STATES finalState) {

	if (!destination || sourceData.empty()) {
		return;
	}
	if (destinationOffset + sourceData.size_bytes() >
		destination->GetDesc().Width) {

		Assert::Call(false,
			"BufferUpload destination range overflow.");
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

	// 更新済みリソースも扱えるよう、呼び出し元が管理する状態からCOPY_DESTへ遷移する
	if (currentState != D3D12_RESOURCE_STATE_COPY_DEST) {

		D3D12_RESOURCE_BARRIER copyDestBarrier{};
		copyDestBarrier.Type =
			D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		copyDestBarrier.Transition.pResource = destination;
		copyDestBarrier.Transition.StateBefore = currentState;
		copyDestBarrier.Transition.StateAfter =
			D3D12_RESOURCE_STATE_COPY_DEST;
		copyDestBarrier.Transition.Subresource =
			D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		currentContext_->commandList->ResourceBarrier(
			1, &copyDestBarrier);
	}

	// staging -> DEFAULT heapへコピー
	currentContext_->commandList->CopyBufferRegion(
		destination, destinationOffset,
		staging.Get(), 0, sourceData.size_bytes());

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
	const HRESULT signalResult = uploadQueue_->Signal(fence_.Get(), submittedFenceValue);
	if (!DxDredDiagnostics::CheckHRESULT(device_, signalResult, "BufferUploadService::SubmitBatch/Signal")) {
		Assert::Call(false, "BufferUpload queue Signal failed.");
	}

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
	pendingBatches_.emplace_back(std::move(pending));

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
		pendingBatches_.pop_front();
	}
}

void Engine::BufferUploadService::FlushAndWait() {

	if (batchOpened_) {
		SubmitBatch();
	}
	WaitForAllUploads();
	TickFinalize();
}

void Engine::BufferUploadService::WaitForFenceValue(uint64_t fenceValue) {

	if (!fence_ || fenceValue == 0) {
		return;
	}
	if (fenceValue <= fence_->GetCompletedValue()) {
		return;
	}
	const HRESULT completionResult = fence_->SetEventOnCompletion(fenceValue, fenceEvent_);
	if (!DxDredDiagnostics::CheckHRESULT(device_, completionResult, "BufferUploadService::WaitForFenceValue/SetEventOnCompletion")) {
		Assert::Call(false, "BufferUpload fence SetEventOnCompletion failed.");
	}

	while (fence_->GetCompletedValue() < fenceValue) {
		constexpr DWORD kWaitSliceMilliseconds = 250u;
		const DWORD waitResult = WaitForSingleObject(fenceEvent_, kWaitSliceMilliseconds);

		if (waitResult == WAIT_OBJECT_0) {
			continue;
		}

		if (waitResult == WAIT_TIMEOUT) {
			if (!DxDredDiagnostics::CheckDeviceState(device_, "BufferUploadService::WaitForFenceValue/Wait")) {
				return;
			}
			continue;
		}

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[D3D12] BufferUpload fence wait failed. WaitResult={}", static_cast<uint32_t>(waitResult));
		return;
	}
}

void Engine::BufferUploadService::WaitForAllUploads() {

	WaitForFenceValue(lastSubmittedFenceValue_);
}
