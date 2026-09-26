#include "DxCommand.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <string>
#include <stdexcept>
#include <utility>

//============================================================================
//	DxCommand classMethods
//============================================================================
namespace {

	// コマンドの失敗時に所属Deviceの診断を残す
	bool CheckCommandResult(ID3D12DeviceChild* object, HRESULT result, const char* operation) {

		if (SUCCEEDED(result)) return true;
		ComPtr<ID3D12Device> device;
		object->GetDevice(IID_PPV_ARGS(&device));
		return DxDredDiagnostics::CheckHRESULT(device.Get(), result, operation);
	}
}

void DxCommand::Create(ID3D12Device* device) {

	if (!device) throw std::invalid_argument("描画コマンドのDeviceが指定されていません");
	if (commandList_) throw std::logic_error("描画コマンドは作成済みです");

	// 全frameのアロケータを候補として作る
	std::array<GraphicsFrameContext, kGraphicsFrameContextCount> contexts{};
	for (uint32_t index = 0; index < kGraphicsFrameContextCount; ++index) {
		auto& context = contexts[index];
		const HRESULT result = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&context.commandAllocator));
		if (!DxDredDiagnostics::CheckHRESULT(device, result, "DxCommand::Create/Allocator")) {
			throw std::runtime_error("描画コマンドアロケータの作成に失敗しました");
		}
		context.commandAllocator->SetName((L"MainGraphicsCommandAllocator[" + std::to_wstring(index) + L"]").c_str());
	}

	ComPtr<ID3D12GraphicsCommandList6> commands;
	const HRESULT result = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, contexts[0].commandAllocator.Get(),
		nullptr, IID_PPV_ARGS(&commands));
	if (!DxDredDiagnostics::CheckHRESULT(device, result, "DxCommand::Create/List")) {
		throw std::runtime_error("描画コマンドリストの作成に失敗しました");
	}
	commands->SetName(L"MainGraphicsCommandList");

	// 作成に成功した組だけを公開する
	frameContexts_ = std::move(contexts);
	commandList_ = std::move(commands);
	currentFrameIndex_ = 0;
	GraphicsFrameState::SetCurrentIndex(currentFrameIndex_);
	recording_ = true;
}

void DxCommand::BeginFrame(uint32_t frameIndex) {

	frameIndex %= GraphicsFrameState::GetActiveCount();
	GraphicsFrameState::BeginFrame(frameIndex);
	if (recording_) {
		if (currentFrameIndex_ != frameIndex) throw std::logic_error("記録中のGraphicsFrameContextと開始要求が一致しません");
		return;
	}

	currentFrameIndex_ = frameIndex;
	ResetCommandList();
}

void DxCommand::CloseCommandList() {

	if (!recording_) {
		return;
	}
	HRESULT hr = commandList_->Close();
	recording_ = false;
	if (!CheckCommandResult(commandList_.Get(), hr, "DxCommand::Close")) {
		throw std::runtime_error("描画コマンドリストを閉じられませんでした");
	}
}

void DxCommand::ResetCommandList() {

	if (!commandList_) throw std::logic_error("描画コマンドが作成されていません");
	recording_ = false;
	// Resetが成功を返す環境でもDevice消失後は記録しない
	ComPtr<ID3D12Device> device;
	commandList_->GetDevice(IID_PPV_ARGS(&device));
	if (!DxDredDiagnostics::CheckDeviceState(device.Get(), "DxCommand::Reset/Device")) {
		throw std::runtime_error("Deviceが失われたため描画を再開できません");
	}
	// アロケータとリストの両方が再利用できた場合だけ記録を許可する
	GraphicsFrameContext& context = frameContexts_[currentFrameIndex_];
	HRESULT hr = context.commandAllocator->Reset();
	if (!CheckCommandResult(context.commandAllocator.Get(), hr, "DxCommand::Reset/Allocator")) {
		throw std::runtime_error("描画コマンドアロケータのリセットに失敗しました");
	}
	hr = commandList_->Reset(context.commandAllocator.Get(), nullptr);
	if (!CheckCommandResult(commandList_.Get(), hr, "DxCommand::Reset/List")) {
		throw std::runtime_error("描画コマンドリストのリセットに失敗しました");
	}
	recording_ = true;
}

void DxCommand::SetCurrentFrameFenceValue(uint64_t fenceValue) {

	frameContexts_[currentFrameIndex_].fenceValue = fenceValue;
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

			Assert::Call(FALSE, "ShadowMapを解除する描画先がありません");
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

			Assert::Call(FALSE, "RTVとDSVのどちらも指定されていません");
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

//============================================================================
//	DxCommand classMethods
//============================================================================

namespace Engine {

	uint64_t DxCommand::GetFrameFenceValue(uint32_t frameIndex) const {

		return frameContexts_[
			frameIndex %
			GraphicsFrameState::GetActiveCount()].fenceValue;
	}
}
