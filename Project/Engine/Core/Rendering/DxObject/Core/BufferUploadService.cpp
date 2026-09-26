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
#include <stdexcept>
#include <exception>

//============================================================================
//	BufferUploadService classMethods
//============================================================================
Engine::BufferUploadService::~BufferUploadService() {

	// 所有元が途中で終了しても提出済みの転送を待つ
	try {
		Finalize();
	} catch (...) {
		if (device_ && FAILED(device_->GetDeviceRemovedReason())) {
			// Device消失後は待機をせず残った資源を解放する
			Finalize();
		} else {
			OutputDebugStringW(L"Buffer転送の完了を確認できないため終了します\n");
			std::terminate();
		}
	}
}

void Engine::BufferUploadService::Init(GraphicsResourceRetirement& retirement, ID3D12Device* device, ID3D12CommandQueue* graphicsQueue) {

	Finalize();
	if (!device) throw std::invalid_argument("BufferUploadのDeviceが指定されていません");

	retirement_ = &retirement;
	device_ = device;
	graphicsQueue_ = graphicsQueue;

	// 転送はCopyBufferRegionとBarrierを1リストで扱いたいのでDIRECTキューにする
	D3D12_COMMAND_QUEUE_DESC queueDesc{};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	HRESULT hr = device_->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&uploadQueue_));
	if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService")) {
		throw std::runtime_error("BufferUpload用コマンドキューの作成に失敗しました");
	}
	uploadQueue_->SetName(L"BufferUploadQueue");

	// アロケータReset待ちを避けるため複数コンテキストをリングで持つ
	contexts_.resize(kUploadContextCount);
	for (size_t i = 0; i < contexts_.size(); ++i) {
		UploadFrameContext& context = contexts_[i];
		hr = device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context.allocator));
		if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService")) {
			throw std::runtime_error("BufferUpload用コマンドアロケータの作成に失敗しました");
		}
		context.allocator->SetName((L"BufferUploadCommandAllocator[" + std::to_wstring(i) + L"]").c_str());

		hr = device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, context.allocator.Get(), nullptr,
			IID_PPV_ARGS(&context.commandList));
		if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService")) {
			throw std::runtime_error("BufferUpload用コマンドリストの作成に失敗しました");
		}
		context.commandList->SetName((L"BufferUploadCommandList[" + std::to_wstring(i) + L"]").c_str());

		// 作成直後は記録状態なので、BeginBatchでResetできるよう一旦閉じる
		hr = context.commandList->Close();
		if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService::Init/Close")) {
			throw std::runtime_error("BufferUpload用コマンドリストの初期化に失敗しました");
		}
		context.lastFenceValue = 0;
	}

	fence_ = nullptr;
	nextFenceValue_ = 1;
	lastSubmittedFenceValue_ = 0;
	hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService")) {
		throw std::runtime_error("BufferUpload用Fenceの作成に失敗しました");
	}
	fence_->SetName(L"BufferUploadFence");

	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!fenceEvent_) throw std::runtime_error("BufferUpload用Fenceの待機イベント作成に失敗しました");

	contextIndex_ = 0;
	currentContext_ = nullptr;
	batchOpened_ = false;
	hasCommands_ = false;
}

void Engine::BufferUploadService::Finalize() {

	// 未SubmitのBatchがあれば終了処理前に閉じ、stagingやCommandListを開いたまま残さない
	if (batchOpened_ && currentContext_ && SUCCEEDED(device_->GetDeviceRemovedReason())) {
		SubmitBatch();
	}

	// Signal失敗後も提出済み資源の完了を確定する
	if (currentContext_ && currentContext_->lastFenceValue == UINT64_MAX && SUCCEEDED(device_->GetDeviceRemovedReason())) {
		SignalSubmittedBatch();
	}

	// GPU使用中のstagingを巻き込まないよう、未完了分を待ってから解放する
	if (fence_ && SUCCEEDED(device_->GetDeviceRemovedReason())) {
		WaitForAllUploads();
	}
	pendingBatches_.clear();
	for (UploadFrameContext& context : contexts_) {
		context.retainedResources.clear();
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
	retirement_ = nullptr;
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

	if (!fence_ || !fenceEvent_ || contexts_.size() != kUploadContextCount) {
		throw std::logic_error("BufferUploadが初期化されていません");
	}
	// 次に使うコンテキストを選び、そのコンテキストのGPU完了を待ってからResetする
	UploadFrameContext& context = contexts_[contextIndex_];
	WaitForFenceValue(context.lastFenceValue);

	HRESULT hr = context.allocator->Reset();
	if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService")) {
		throw std::runtime_error("BufferUpload用コマンドアロケータのリセットに失敗しました");
	}
	hr = context.commandList->Reset(context.allocator.Get(), nullptr);
	if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService")) {
		throw std::runtime_error("BufferUpload用コマンドリストのリセットに失敗しました");
	}

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
	const uint64_t capacity = destination->GetDesc().Width;
	if (destinationOffset > capacity || sourceData.size_bytes() > capacity - destinationOffset) {

		throw std::out_of_range("BufferUploadの書き込み範囲が転送先容量を超えています");
	}

	EnsureBatchOpened();

	// CPUデータをUPLOAD heap stagingへ書き込む
	ComPtr<ID3D12Resource> staging;
	DxUtils::CreateUploadBufferResource(device_, staging, sourceData.size_bytes());

	void* mapped = nullptr;
	HRESULT hr = staging->Map(0, nullptr, &mapped);
	if (!DxDredDiagnostics::CheckHRESULT(device_, hr, "BufferUploadService::EnqueueBufferUpload/Map")) {
		throw std::runtime_error("BufferUpload用ステージングバッファのMapに失敗しました");
	}
	std::memcpy(mapped, sourceData.data(), sourceData.size_bytes());
	staging->Unmap(0, nullptr);

	// 記録後の所有登録で追加確保が発生しないようにする
	currentContext_->retainedResources.reserve(currentContext_->retainedResources.size() + 2);

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

	// 未提出期間を含め転送先もFence完了まで保持する
	currentContext_->retainedResources.emplace_back(destination);
	currentContext_->retainedResources.emplace_back(std::move(staging));
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

	if (nextFenceValue_ == UINT64_MAX) throw std::overflow_error("BufferUploadのFence値が上限に達しました");
	// 提出後に保持用コンテナの確保を行わない
	pendingBatches_.emplace_back();
	const HRESULT closed = currentContext_->commandList->Close();
	batchOpened_ = false;
	hasCommands_ = false;
	if (!DxDredDiagnostics::CheckHRESULT(device_, closed, "BufferUploadService::Close")) {
		pendingBatches_.pop_back();
		throw std::runtime_error("BufferUpload用コマンドリストを閉じられませんでした");
	}
	auto& pending = pendingBatches_.back();
	pending.fenceValue = UINT64_MAX;
	pending.retainedResources = std::move(currentContext_->retainedResources);
	currentContext_->lastFenceValue = UINT64_MAX;
	ID3D12CommandList* lists[] = { currentContext_->commandList.Get() };
	uploadQueue_->ExecuteCommandLists(1, lists);
	const uint64_t submitted = SignalSubmittedBatch();

	// 描画側の待機失敗でも提出済みBatchを再実行しない
	if (graphicsQueue_ && graphicsQueue_ != uploadQueue_.Get()) {
		if (!DxDredDiagnostics::CheckHRESULT(device_, graphicsQueue_->Wait(fence_.Get(), submitted), "BufferUploadService::QueueWait")) {
			throw std::runtime_error("描画キューのBuffer転送待機に失敗しました");
		}
	}
	return submitted;
}

uint64_t Engine::BufferUploadService::SignalSubmittedBatch() {

	const uint64_t submitted = nextFenceValue_;
	if (!DxDredDiagnostics::CheckHRESULT(device_, uploadQueue_->Signal(fence_.Get(), submitted), "BufferUploadService::Signal")) {
		throw std::runtime_error("BufferUpload用コマンドキューのSignalに失敗しました");
	}
	// Fenceを発行できたBatchだけ再利用可能にする
	++nextFenceValue_;
	lastSubmittedFenceValue_ = submitted;
	currentContext_->lastFenceValue = submitted;
	pendingBatches_.back().fenceValue = submitted;
	contextIndex_ = (contextIndex_ + 1) % kUploadContextCount;
	currentContext_ = nullptr;
	return submitted;
}

void Engine::BufferUploadService::TickFinalize() {

	if (!fence_) {
		return;
	}

	const uint64_t completed = fence_->GetCompletedValue();
	if (completed == UINT64_MAX) {
		DxDredDiagnostics::DumpDeviceRemovedData(device_, "BufferUploadService::TickFinalize");
		throw std::runtime_error("BufferUpload中にDeviceが失われました");
	}
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

	if (fenceValue == 0) {
		return;
	}
	if (!DxDredDiagnostics::WaitForFence(device_, fence_.Get(), fenceValue, fenceEvent_, "BufferUploadService::Wait")) {
		throw std::runtime_error("BufferUploadの完了を確認できませんでした");
	}
}

void Engine::BufferUploadService::WaitForAllUploads() {

	WaitForFenceValue(lastSubmittedFenceValue_);
}

Engine::GraphicsResourceRetirement& Engine::BufferUploadService::GetResourceRetirement() const {

	if (!retirement_) throw std::logic_error("BufferUploadの回収先が設定されていません");
	return *retirement_;
}
