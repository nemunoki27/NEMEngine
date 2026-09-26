#include "DxCommandQueue.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <stdexcept>

//============================================================================
//	DxCommandQueue classMethods
//============================================================================
DxCommandQueue::~DxCommandQueue() {

	Finalize();
}

void DxCommandQueue::Create(ID3D12Device* device) {

	if (!device) throw std::invalid_argument("描画キューのDeviceが指定されていません");
	if (device_) throw std::logic_error("描画キューは終了後に再生成してください");
	device_ = device;

	commandQueue_ = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	HRESULT hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	if (!DxDredDiagnostics::CheckHRESULT(device, hr, "DxCommandQueue::Create")) {
		throw std::runtime_error("描画コマンドキューの作成に失敗しました");
	}
	commandQueue_->SetName(L"MainGraphicsQueue");

	fence_ = nullptr;
	fenceValue_ = 0;
	hr = device->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	if (!DxDredDiagnostics::CheckHRESULT(device, hr, "DxCommandQueue::Create")) {
		throw std::runtime_error("描画Fenceの作成に失敗しました");
	}
	fence_->SetName(L"MainGraphicsFence");

	// FenceのSignalを待つためのイベントの作成する
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!fenceEvent_) {
		throw std::runtime_error("描画Fenceの待機イベント作成に失敗しました");
	}
}

void DxCommandQueue::Finalize() {

	if (fenceEvent_) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}
	fence_.Reset();
	commandQueue_.Reset();
	device_.Reset();
}

void DxCommandQueue::ExecuteCommandList(ID3D12GraphicsCommandList6* commandList) {

	ID3D12CommandList* commandLists[] = { commandList };
	commandQueue_->ExecuteCommandLists(1, commandLists);
}

void DxCommandQueue::SignalAndWait() {

	const uint64_t fenceValue = Signal();
	if (fenceValue == 0) {
		return;
	}

	// 実行完了を待つ
	if (!WaitForFenceValue(fenceValue, "DxCommandQueue::SignalAndWait/Wait")) {
		throw std::runtime_error("GPUの完了を確認できませんでした");
	}
}

uint64_t DxCommandQueue::Signal() {

	if (fenceValue_ >= UINT64_MAX - 1) throw std::overflow_error("描画Fence値が上限に達しました");
	const uint64_t fenceValue = fenceValue_ + 1;
	const HRESULT signalResult = commandQueue_->Signal(fence_.Get(), fenceValue);
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), signalResult, "DxCommandQueue::SignalAndWait/Signal")) {
		throw std::runtime_error("描画コマンドキューのSignalに失敗しました");
	}
	fenceValue_ = fenceValue;
	return fenceValue;
}

bool DxCommandQueue::WaitForFenceValue(uint64_t expectedValue, std::string_view operation) {

	return DxDredDiagnostics::WaitForFence(device_.Get(), fence_.Get(), expectedValue, fenceEvent_, operation);
}
