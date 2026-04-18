#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/DxLib/DxStructures.h>
#include <Engine/Core/Graphics/DxLib/ComPtr.h>

// directX
#include <d3d12.h>
#include <dxgi1_6.h>
// c++
#include <cstdint>
#include <vector>
#include <array>
#include <optional>
#include <chrono>
#include <thread>
#include <future>

//============================================================================
//	DxCommand class
//	Direct3D12のコマンドキュー/アロケータ/リストを管理し、実行と同期を提供する。
//============================================================================
namespace Engine {

class DxCommand {
public:
	//========================================================================
	//	public Methods
	//========================================================================

	DxCommand() = default;
	~DxCommand() = default;

	// デバイスからキュー/アロケータ/リスト/フェンスを生成し初期化する
	void Create(ID3D12Device* device);

	// コマンドをキューへ提出する（必要に応じてPresent前の処理に備える）
	void ExecuteCommands(IDXGISwapChain4* swapChain);

	// フェンスを用いてGPU完了まで待機する
	void WaitForGPU();

	// 終了処理: フェンス/イベント等のリソースを破棄する
	void Finalize(HWND hwnd);

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

	// リソースバリア遷移処理
	void TransitionBarriers(const std::vector<ID3D12Resource*>& resources,
		D3D12_RESOURCE_STATES stateBefore, D3D12_RESOURCE_STATES stateAfter);

	// UAVの書き込み順序保証のため対象にUAVバリアを発行する
	void UAVBarrier(ID3D12Resource* resource);
	void UAVBarrierAll();

	// リソースコピー処理
	void CopyTexture(ID3D12Resource* dstResource, D3D12_RESOURCE_STATES dstState,
		ID3D12Resource* srcResource, D3D12_RESOURCE_STATES srcState);

	//--------- accessor -----------------------------------------------------

	ID3D12CommandQueue* GetQueue() const { return commandQueue_.Get(); }
	ID3D12GraphicsCommandList6* GetCommandList() const { return commandList_.Get(); }
private:
	//========================================================================
	//	private Methods
	//========================================================================

	//--------- variables ----------------------------------------------------

	ComPtr<ID3D12GraphicsCommandList6> commandList_;
	ComPtr<ID3D12CommandAllocator> commandAllocator_;

	ComPtr<ID3D12CommandQueue> commandQueue_;

	ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_;
	HANDLE fenceEvent_;

	std::chrono::steady_clock::time_point reference_;

	//--------- functions ----------------------------------------------------

	// グラフィックスパスのコマンドを提出する
	void ExecuteGraphicsCommands(IDXGISwapChain4* swapChain);
	// フェンス値をシグナルし、イベントによる待機を設定する
	void FenceEvent();
	// 固定FPS向けにCPU側の待機/時間調整を行う
	void UpdateFixFPS();
};

}; // Engine
