#include "DxCommand.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>

// c++
#include <cassert>

//============================================================================
//	DxCommand classMethods
//============================================================================
void DxCommand::Create(ID3D12Device* device) {

	commandAllocator_ = nullptr;
	HRESULT hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator_));
	assert(SUCCEEDED(hr));
	commandAllocator_->SetName(L"MainGraphicsCommandAllocator");

	commandList_ = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator_.Get(), nullptr, IID_PPV_ARGS(&commandList_));
	assert(SUCCEEDED(hr));
	commandList_->SetName(L"MainGraphicsCommandList");
}

void DxCommand::CloseCommandList() {

	HRESULT hr = commandList_->Close();
	assert(SUCCEEDED(hr));
}

void DxCommand::ResetCommandList() {

	HRESULT hr = commandAllocator_->Reset();
	assert(SUCCEEDED(hr));
	hr = commandList_->Reset(commandAllocator_.Get(), nullptr);
	assert(SUCCEEDED(hr));
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

	SetViewportAndScissor(0, 0, width, height);
}

void DxCommand::SetViewportAndScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {

	D3D12_VIEWPORT viewport{};
	D3D12_RECT scissorRect{};

	viewport =
		D3D12_VIEWPORT(FLOAT(x), FLOAT(y), FLOAT(width), FLOAT(height), 0.0f, 1.0f);
	commandList_->RSSetViewports(1, &viewport);

	scissorRect = D3D12_RECT(static_cast<LONG>(x), static_cast<LONG>(y),
		static_cast<LONG>(x + width), static_cast<LONG>(y + height));
	commandList_->RSSetScissorRects(1, &scissorRect);
}

void DxCommand::TransitionBarriers(ID3D12Resource* resource,
	D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter) {

	// 単一リソースは一時vectorを作らずスタック上のバリア1つで遷移する
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.StateBefore = stateBefore;
	barrier.Transition.StateAfter = stateAfter;

	commandList_->ResourceBarrier(1, &barrier);
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
		// バリアを貼る対象のリソースで引数で渡されたリソースに対して行う
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
	TransitionBarriers(srcResource, srcState, D3D12_RESOURCE_STATE_COPY_SOURCE);
	TransitionBarriers(dstResource, dstState, D3D12_RESOURCE_STATE_COPY_DEST);

	commandList_->CopyResource(dstResource, srcResource);

	// 元の状態に戻す
	TransitionBarriers(srcResource, D3D12_RESOURCE_STATE_COPY_SOURCE, srcState);
	TransitionBarriers(dstResource, D3D12_RESOURCE_STATE_COPY_DEST, dstState);
}
