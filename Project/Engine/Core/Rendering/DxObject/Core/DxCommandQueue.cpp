#include "DxCommandQueue.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <cassert>

//============================================================================
//	DxCommandQueue classMethods
//============================================================================
void DxCommandQueue::Create(ID3D12Device* device) {

	device_ = device;

	commandQueue_ = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	HRESULT hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	assert(SUCCEEDED(hr));
	commandQueue_->SetName(L"MainGraphicsQueue");

	fence_ = nullptr;
	fenceValue_ = 0;
	hr = device->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	assert(SUCCEEDED(hr));
	fence_->SetName(L"MainGraphicsFence");

	// FenceのSignalを待つためのイベントの作成する
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent_ != nullptr);
}

void DxCommandQueue::Finalize() {

	if (fenceEvent_) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}
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
	WaitForFenceValue(fenceValue, "DxCommandQueue::SignalAndWait/Wait");
}

uint64_t DxCommandQueue::Signal() {

	const uint64_t fenceValue = ++fenceValue_;
	const HRESULT signalResult = commandQueue_->Signal(fence_.Get(), fenceValue);
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), signalResult, "DxCommandQueue::SignalAndWait/Signal")) {
		Assert::Call(false, "Graphics queue Signal failed.");
		return 0;
	}
	return fenceValue;
}

bool DxCommandQueue::WaitForFenceValue(uint64_t expectedValue, std::string_view operation) {

	if (expectedValue == 0 || fence_->GetCompletedValue() >= expectedValue) {
		return true;
	}

	const HRESULT completionResult = fence_->SetEventOnCompletion(expectedValue, fenceEvent_);
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), completionResult,
		"DxCommandQueue::WaitForFenceValue/SetEventOnCompletion")) {
		Assert::Call(false, "Fence SetEventOnCompletion failed.");
		return false;
	}

	while (fence_->GetCompletedValue() < expectedValue) {
		constexpr DWORD kWaitSliceMilliseconds = 250u;
		const DWORD waitResult = WaitForSingleObject(fenceEvent_, kWaitSliceMilliseconds);

		if (waitResult == WAIT_OBJECT_0) {
			continue;
		}

		if (waitResult == WAIT_TIMEOUT) {
			if (!DxDredDiagnostics::CheckDeviceState(device_.Get(), operation)) {
				return false;
			}
			continue;
		}

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[D3D12] Fence wait failed. Operation='{}' WaitResult={}",
			operation, static_cast<uint32_t>(waitResult));
		return false;
	}
	return true;
}
