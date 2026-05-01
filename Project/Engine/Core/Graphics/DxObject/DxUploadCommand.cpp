#include "DxUploadCommand.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	DxUploadCommand classMethods
//============================================================================

void DxUploadCommand::Create(ID3D12Device* device) {

	fence_ = nullptr;
	fenceValue_ = 0;
	HRESULT hr = device->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントの作成する
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent_ != nullptr);

	commandQueue_ = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	assert(SUCCEEDED(hr));

	commandAllocator_ = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	assert(SUCCEEDED(hr));

	commandList_ = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	assert(SUCCEEDED(hr));
}

void DxUploadCommand::ExecuteCommands(ID3D12CommandQueue* waitQueue) {

	// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseする
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// GPUにコマンドリストの実行を行わせる
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// Feneceの値を更新
	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// 描画キュー側でもアップロード完了を待つ
	if (waitQueue && waitQueue != commandQueue_.Get()) {
		waitQueue->Wait(fence_.Get(), fenceValue_);
	}

	// Fenceの値が指定したSignal値にたどり着いているか確認する
	if (fence_->GetCompletedValue() < fenceValue_) {

		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		// イベントを待つ
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
	ResetCommand();
}

void DxUploadCommand::ResetCommand() {

	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}
