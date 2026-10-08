#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxTypes.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>
#include <cstdint>
#include <vector>
#include <optional>

// directX
#include <d3d12.h>

//============================================================================
//	DxCommand class
// コマンドリストとアロケータを保持し描画コマンドの記録を提供する
//============================================================================
namespace Engine {

class DxCommand {
public:
	//============================================================================
	//	public Methods
	//============================================================================

	DxCommand() = default;
	~DxCommand() = default;
	DxCommand(const DxCommand&) = delete;
	DxCommand& operator=(const DxCommand&) = delete;

	// デバイスからアロケータ/リストを生成し初期化する
	void Create(ID3D12Device* device);

	// 再利用可能になったフレームコンテキストで記録を開始する
	void BeginFrame(uint32_t frameIndex);
	// 提出前にコマンドリストを閉じる
	void CloseCommandList();
	// 次フレーム用にアロケータとコマンドリストをリセットする
	void ResetCommandList();
	// 現在のフレームへ提出Fence値を記録する
	void SetCurrentFrameFenceValue(uint64_t fenceValue);

	// ルートで使用するディスクリプタヒープ配列をセットする
	void SetDescriptorHeaps(const std::vector<ID3D12DescriptorHeap*>& descriptorHeaps);

	// レンダーターゲットの設定
	void SetRenderTargets(const std::optional<RenderTarget>& renderTarget,
		const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle = std::nullopt);
	void SetRenderTargets(const std::vector<RenderTarget>& renderTargets,
		const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle = std::nullopt);
	void BindRenderTargets(const std::optional<RenderTarget>& renderTarget,
		const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle = std::nullopt);
	void BindRenderTargets(const std::vector<RenderTarget>& renderTargets,
		const std::optional<D3D12_CPU_DESCRIPTOR_HANDLE>& dsvHandle = std::nullopt);

	// 指定DSVをクリアする
	void ClearDepthStencilView(const D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle);

	// 指定サイズでビューポート/シザーを設定する
	void SetViewportAndScissor(uint32_t width, uint32_t height);
	// 指定矩形でビューポート/シザーを設定する
	void SetViewportAndScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height);

	// リソースバリア遷移処理
	void TransitionBarriers(ID3D12Resource* resource,
		D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);
	void TransitionBarriers(const std::vector<ID3D12Resource*>& resources,
		D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);

	// UAVの書き込み順序保証のため対象にUAVバリアを発行する
	void UAVBarrier(ID3D12Resource* resource);
	void UAVBarrierAll();

	// リソースコピー処理
	void CopyTexture(ID3D12Resource* dstResource, D3D12_RESOURCE_STATES dstState,
		ID3D12Resource* srcResource, D3D12_RESOURCE_STATES srcState);

	//--------- accessor -----------------------------------------------------

	ID3D12GraphicsCommandList6* GetCommandList() const { return commandList_.Get(); }
	uint32_t GetCurrentFrameIndex() const { return currentFrameIndex_; }
	uint64_t GetFrameFenceValue(uint32_t frameIndex) const;
	bool IsRecording() const { return recording_; }
private:
	//============================================================================
	//	private Methods
	//============================================================================

	//--------- variables ----------------------------------------------------

	ComPtr<ID3D12GraphicsCommandList6> commandList_;
	std::array<GraphicsFrameContext, kGraphicsFrameContextCount> frameContexts_{};
	uint32_t currentFrameIndex_ = 0;
	bool recording_ = false;
};

}; // Engine
