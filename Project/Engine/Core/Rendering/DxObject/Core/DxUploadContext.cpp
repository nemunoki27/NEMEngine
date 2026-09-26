#include "DxUploadContext.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <stdexcept>
#include <exception>

//============================================================================
//	DxUploadCommand classMethods
//============================================================================
DxUploadCommand::~DxUploadCommand() {

	// GPUが使用中の転送資源を解放する前に完了を待つ
	try {
		FlushAndWait();
	} catch (...) {
		if (device_ && SUCCEEDED(device_->GetDeviceRemovedReason())) {
			OutputDebugStringW(L"Texture転送の完了を確認できないため終了します\n");
			std::terminate();
		}
	}
	if (fenceEvent_) CloseHandle(fenceEvent_);
}

void DxUploadCommand::Create(ID3D12Device* device) {

	if (!device) throw std::invalid_argument("UploadのDeviceが指定されていません");
	if (device_) throw std::logic_error("UploadCommandは生成済みです");
	device_ = device;

	fence_ = nullptr;
	fenceValue_ = 0;
	HRESULT hr = device->CreateFence(fenceValue_, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), hr, "DxUploadCommand")) {
		throw std::runtime_error("Upload用Fenceの作成に失敗しました");
	}
	fence_->SetName(L"DxUploadFence");

	// FenceのSignalを待つためのイベントの作成する
	fenceEvent_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!fenceEvent_) throw std::runtime_error("Upload用Fenceの待機イベント作成に失敗しました");

	commandQueue_ = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue_));
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), hr, "DxUploadCommand")) {
		throw std::runtime_error("Upload用コマンドキューの作成に失敗しました");
	}
	commandQueue_->SetName(L"DxUploadQueue");

	commandAllocator_ = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), hr, "DxUploadCommand")) {
		throw std::runtime_error("Upload用コマンドアロケータの作成に失敗しました");
	}
	commandAllocator_->SetName(L"DxUploadCommandAllocator");

	commandList_ = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), hr, "DxUploadCommand")) {
		throw std::runtime_error("Upload用コマンドリストの作成に失敗しました");
	}
	commandList_->SetName(L"DxUploadCommandList");
	recording_ = true;
}

ID3D12GraphicsCommandList* DxUploadCommand::GetCommandList() const {

	if (!recording_) throw std::logic_error("UploadCommandは記録可能な状態ではありません");
	return commandList_.Get();
}

void DxUploadCommand::ExecuteCommands(std::span<const ComPtr<ID3D12Resource>> resources) {

	if (!recording_) throw std::logic_error("UploadCommandは記録可能な状態ではありません");
	if (fenceValue_ >= UINT64_MAX - 1) throw std::overflow_error("UploadのFence値が上限に達しました");

	// 提出後の失敗に備えて転送元と転送先を保持する
	recording_ = false;
	if (!DxDredDiagnostics::CheckDeviceState(device_.Get(), "DxUploadCommand::Execute/Device")) {
		throw std::runtime_error("Deviceが失われたためTexture転送を提出できません");
	}
	retainedResources_.assign(resources.begin(), resources.end());
	const HRESULT closeResult = commandList_->Close();
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), closeResult, "DxUploadCommand::Close")) {
		throw std::runtime_error("Upload用コマンドリストを閉じられませんでした");
	}

	// 同じ転送を再提出せず、Signalと完了待機だけを再試行する
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	submitted_ = true;
	commandQueue_->ExecuteCommandLists(1, commandLists);
	FlushAndWait();
	ResetCommand();
}

void DxUploadCommand::FlushAndWait() {

	if (!submitted_) return;
	if (!signaled_) {
		// Signalに成功した値だけを公開する
		const uint64_t nextValue = fenceValue_ + 1;
		const HRESULT signalResult = commandQueue_->Signal(fence_.Get(), nextValue);
		if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), signalResult, "DxUploadCommand::Signal")) {
			throw std::runtime_error("Upload用コマンドキューのSignalに失敗しました");
		}
		fenceValue_ = nextValue;
		signaled_ = true;
	}

	// CPU側で完了を待ち、公開前に転送を確定する
	if (!DxDredDiagnostics::WaitForFence(device_.Get(), fence_.Get(), fenceValue_, fenceEvent_, "DxUploadCommand::Wait")) {
		throw std::runtime_error("Texture転送の完了を確認できませんでした");
	}
	submitted_ = false;
	signaled_ = false;
	retainedResources_.clear();
}

void DxUploadCommand::ResetCommand() {

	HRESULT hr = commandAllocator_->Reset();
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), hr, "DxUploadCommand")) {
		throw std::runtime_error("Upload用コマンドアロケータのリセットに失敗しました");
	}
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	if (!DxDredDiagnostics::CheckHRESULT(device_.Get(), hr, "DxUploadCommand")) {
		throw std::runtime_error("Upload用コマンドリストのリセットに失敗しました");
	}
	recording_ = true;
}
