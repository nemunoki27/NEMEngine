#include "DxUploadContext.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

//============================================================================
//	DxUploadCommand classMethods
//============================================================================
void DxUploadCommand::Create(ID3D12Device* device) {

	device_ = device;

	fence_ = nullptr;
	fenceValue_ = 0;
	HRESULT hr = device->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	Assert::Call(SUCCEEDED(hr), "Upload用Fenceの作成に失敗しました");
	fence_->SetName(L"DxUploadFence");

	// FenceのSignalを待つためのイベントの作成する
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	Assert::Call(fenceEvent_ != nullptr, "Upload用Fenceの待機イベント作成に失敗しました");

	commandQueue_ = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	Assert::Call(SUCCEEDED(hr), "Upload用コマンドキューの作成に失敗しました");
	commandQueue_->SetName(L"DxUploadQueue");

	commandAllocator_ = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	Assert::Call(SUCCEEDED(hr), "Upload用コマンドアロケータの作成に失敗しました");
	commandAllocator_->SetName(L"DxUploadCommandAllocator");

	commandList_ = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	Assert::Call(SUCCEEDED(hr), "Upload用コマンドリストの作成に失敗しました");
	commandList_->SetName(L"DxUploadCommandList");
}

void DxUploadCommand::ExecuteCommands(ID3D12CommandQueue* waitQueue) {

	// コマンドリストの内容を確定させ、すべてのコマンドを積んでからCloseする
	HRESULT hr = commandList_->Close();
	Assert::Call(SUCCEEDED(hr), "Upload用コマンドリストを閉じられませんでした");

	// GPUにコマンドリストの実行を行わせる
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// Feneceの値を更新
	fenceValue_++;
	const HRESULT signalResult = commandQueue_->Signal(fence_.Get(), fenceValue_);
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), signalResult, "DxUploadCommand::ExecuteCommands/Signal")) {
		Assert::Call(false, "Upload用コマンドキューのSignalに失敗しました");
	}

	// 描画キュー側でもアップロード完了を待つ
	if (waitQueue && waitQueue != commandQueue_.Get()) {
		waitQueue->Wait(fence_.Get(), fenceValue_);
	}

	// Fenceの値が指定したSignal値にたどり着いているか確認する
	if (fence_->GetCompletedValue() < fenceValue_) {

		const HRESULT completionResult = fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), completionResult, "DxUploadCommand::ExecuteCommands/SetEventOnCompletion")) {
			Assert::Call(false, "Upload用Fenceの完了イベント設定に失敗しました");
		}

		// イベントを待つ
		while (fence_->GetCompletedValue() < fenceValue_) {
			constexpr DWORD kWaitSliceMilliseconds = 250u;
			const DWORD waitResult = WaitForSingleObject(fenceEvent_, kWaitSliceMilliseconds);

			if (waitResult == WAIT_OBJECT_0) {
				continue;
			}

			if (waitResult == WAIT_TIMEOUT) {
				if (!DxDredDiagnostics::CheckDeviceState(device_.Get(), "DxUploadCommand::ExecuteCommands/Wait")) {
					return;
				}
				continue;
			}

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[D3D12] Upload Fenceの待機に失敗しました WaitResult={}",
				static_cast<uint32_t>(waitResult));
			return;
		}
	}
	ResetCommand();
}

void DxUploadCommand::ResetCommand() {

	HRESULT hr = commandAllocator_->Reset();
	Assert::Call(SUCCEEDED(hr), "Upload用コマンドアロケータのリセットに失敗しました");
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	Assert::Call(SUCCEEDED(hr), "Upload用コマンドリストのリセットに失敗しました");
}
