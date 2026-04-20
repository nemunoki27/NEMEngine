#include "DxCommand.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>

//============================================================================
//	DxCommand classMethods
//============================================================================

void DxCommand::UpdateFixFPS() {

	// フレームレートピッタリの時間
	constexpr std::chrono::microseconds kMinTime(static_cast<uint64_t>(1000000.0f / 60.0f));

	// 1/60秒よりわずかに短い時間
	constexpr std::chrono::microseconds kMinCheckTime(uint64_t(1000000.0f / 64.0f));

	// 現在時間を取得する
	auto now = std::chrono::steady_clock::now();
	// 前回記録からの経過時間を取得する
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

	// 1/60秒 (よりわずかに短い時間) 経っていない場合
	if (elapsed < kMinCheckTime) {
		// 1/60秒経過するまで微小なスリープを繰り返す
		auto wait_until = reference_ + kMinTime;
		while (std::chrono::steady_clock::now() < wait_until) {
			std::this_thread::yield();
		}
	}

	// 現在の時間を記録する
	reference_ = std::chrono::steady_clock::now();
}

void DxCommand::Create(ID3D12Device* device) {

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

	reference_ = std::chrono::steady_clock::now();
}

void DxCommand::ExecuteGraphicsCommands(IDXGISwapChain4* swapChain) {

	HRESULT hr = S_OK;
	hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// GPUとOSに画面の交換を行うように通知する
	swapChain->Present(0, DXGI_PRESENT_ALLOW_TEARING);
	//swapChain->Present(1, 0);
}

void DxCommand::FenceEvent() {

	// Fenceの値を更新
	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// 実行完了を待つ
	if (fence_->GetCompletedValue() < fenceValue_) {

		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		// イベントを待つ
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
}

void DxCommand::ExecuteCommands(IDXGISwapChain4* swapChain) {

	// GraphicsCommand を実行
	ExecuteGraphicsCommands(swapChain);
	// Graphicsの完了を待つ
	FenceEvent();

	// FPS固定
	//UpdateFixFPS();

	// コマンドリストのリセット
	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void DxCommand::WaitForGPU() {

	// コマンドリストの内容を確定させる。すべてのコマンドを積んでからCloseする
	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));

	// GPUにコマンドリストの実行を行わせる
	ID3D12CommandList* commandLists[] = { commandList_.Get() };
	commandQueue_->ExecuteCommandLists(1, commandLists);

	// フェンスの値を更新
	fenceValue_++;
	commandQueue_->Signal(fence_.Get(), fenceValue_);

	// Fenceの値が指定したSignal値にたどり着いているか確認する
	if (fence_->GetCompletedValue() < fenceValue_) {

		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		// イベントを待つ
		WaitForSingleObject(fenceEvent_, INFINITE);
	}

	hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
}

void DxCommand::Finalize(HWND hwnd) {
	CloseHandle(fenceEvent_);
	CloseWindow(hwnd);
}

//============================================================================
//	GraphicsCommand
//============================================================================

void DxCommand::SetDescriptorHeaps(const std::vector<ID3D12DescriptorHeap*>& descriptorHeaps) {

	commandList_->SetDescriptorHeaps(
		static_cast<UINT>(descriptorHeaps.size()), descriptorHeaps.data());
}

void DxCommand::SetRenderTargets(const std::optional<RenderTarget>& renderTarget,
	const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle) {

	// renderTargetが設定されているとき
	if (renderTarget.has_value()) {

		commandList_->OMSetRenderTargets(1,
			&renderTarget.value().rtvHandle, FALSE, dsvHandle.has_value() ? &dsvHandle.value() : nullptr);
		float clearColor[] =
		{ renderTarget.value().clearColor.r, renderTarget.value().clearColor.g,
			renderTarget.value().clearColor.b, renderTarget.value().clearColor.a };
		commandList_->ClearRenderTargetView(renderTarget.value().rtvHandle, clearColor, 0, nullptr);
	} else {
		if (dsvHandle.has_value()) {

			commandList_->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle.value());
		} else {

			Assert::Call(FALSE, "unSetting ShadowMap");
		}
	}
}

void DxCommand::SetRenderTargets(const std::vector<RenderTarget>& renderTargets,
	const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle) {

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> handles;
	handles.reserve(renderTargets.size());
	for (const auto& renderTarget : renderTargets) {

		handles.push_back(renderTarget.rtvHandle);
	}
	commandList_->OMSetRenderTargets(static_cast<UINT>(handles.size()),
		handles.data(), FALSE, dsvHandle.has_value() ? &dsvHandle.value() : nullptr);

	// すべてのRTVをクリア
	for (const auto& renderTarget : renderTargets) {

		float colors[] = { renderTarget.clearColor.r, renderTarget.clearColor.g,
			renderTarget.clearColor.b, renderTarget.clearColor.a };
		commandList_->ClearRenderTargetView(renderTarget.rtvHandle, colors, 0, nullptr);
	}
}

void Engine::DxCommand::BindRenderTargets(const std::optional<RenderTarget>& renderTarget,
	const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle) {

	if (renderTarget.has_value()) {

		commandList_->OMSetRenderTargets(1, &renderTarget.value().rtvHandle,
			FALSE, dsvHandle.has_value() ? &dsvHandle.value() : nullptr);
	} else {
		if (dsvHandle.has_value()) {

			commandList_->OMSetRenderTargets(0, nullptr, FALSE, &dsvHandle.value());
		} else {

			Assert::Call(FALSE, "BindRenderTargets failed : no RTV/DSV");
		}
	}
}

void Engine::DxCommand::BindRenderTargets(const std::vector<RenderTarget>& renderTargets,
	const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle) {

	std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> handles;
	handles.reserve(renderTargets.size());
	for (const auto& renderTarget : renderTargets) {

		handles.push_back(renderTarget.rtvHandle);
	}

	commandList_->OMSetRenderTargets(static_cast<UINT>(handles.size()), handles.data(),
		FALSE, dsvHandle.has_value() ? &dsvHandle.value() : nullptr);
}

void DxCommand::ClearDepthStencilView(const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle) {

	commandList_->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
}

void DxCommand::SetViewportAndScissor(uint32_t width, uint32_t height) {

	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	viewport =
		D3D12_VIEWPORT(0.0f, 0.0f, FLOAT(width), FLOAT(height), 0.0f, 1.0f);
	commandList_->RSSetViewports(1, &viewport);

	scissorRect = D3D12_RECT(0, 0, width, height);
	commandList_->RSSetScissorRects(1, &scissorRect);
}

void DxCommand::TransitionBarriers(const std::vector<ID3D12Resource*>& resources,
	D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter) {

	std::vector<D3D12_RESOURCE_BARRIER> barriers;
	// メモリ確保
	barriers.reserve(resources.size());

	for (const auto& resource : resources) {
		D3D12_RESOURCE_BARRIER barrier{};

		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		// バリアを貼る対象のリソース。引数で渡されたリソースに対して行う
		barrier.Transition.pResource = resource;
		// 遷移前(現在)のResourceState
		barrier.Transition.StateBefore = stateBefore;
		// 遷移後のResourceState
		barrier.Transition.StateAfter = stateAfter;

		barriers.push_back(barrier);
	}

	commandList_->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
}

void DxCommand::UAVBarrier(ID3D12Resource* resource) {

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = resource;
	commandList_->ResourceBarrier(1, &barrier);
}

void DxCommand::UAVBarrierAll() {

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	barrier.UAV.pResource = nullptr;
	commandList_->ResourceBarrier(1, &barrier);
}

void DxCommand::CopyTexture(ID3D12Resource* dstResource, D3D12_RESOURCE_STATES dstState,
	ID3D12Resource* srcResource, D3D12_RESOURCE_STATES srcState) {

	// 状態遷移
	TransitionBarriers({ srcResource }, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE);
	TransitionBarriers({ dstResource }, dstState, D3D12_RESOURCE_STATE_COPY_DEST);

	commandList_->CopyResource(dstResource, srcResource);

	// 元の状態に戻す
	TransitionBarriers({ srcResource }, D3D12_RESOURCE_STATE_COPY_SOURCE, srcState);
	TransitionBarriers({ dstResource }, D3D12_RESOURCE_STATE_COPY_DEST, dstState);
}
