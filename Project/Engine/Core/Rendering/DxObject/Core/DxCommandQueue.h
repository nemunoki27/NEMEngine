#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <d3d12.h>
// c++
#include <cstdint>
#include <string_view>

//============================================================================
//	DxCommandQueue class
// コマンドキューとフェンスを保持しGPU実行と同期を提供する
//============================================================================
namespace Engine {

class DxCommandQueue {
public:
	//============================================================================
	//	public Methods
	//============================================================================

	DxCommandQueue() = default;
	~DxCommandQueue() = default;

	// デバイスからキュー/フェンス/イベントを生成し初期化する
	void Create(ID3D12Device* device);

	// 終了処理:イベントを破棄する
	void Finalize();

	// コマンドリストをキューへ提出する
	void ExecuteCommandList(ID3D12GraphicsCommandList6* commandList);

	// フェンスをシグナルしGPU完了まで待機する
	void SignalAndWait();
	// フェンスをシグナルし発行値を返す
	uint64_t Signal();
	// 指定Fence値までGPU完了を待つ
	bool WaitForFenceValue(uint64_t expectedValue, std::string_view operation);

	//--------- accessor -----------------------------------------------------

	ID3D12CommandQueue* GetQueue() const { return commandQueue_.Get(); }
	uint64_t GetCompletedFenceValue() const { return fence_->GetCompletedValue(); }
	uint64_t GetLastSignaledFenceValue() const { return fenceValue_; }
private:
	//============================================================================
	//	private Methods
	//============================================================================

	//--------- variables ----------------------------------------------------

	ComPtr<ID3D12Device> device_;
	ComPtr<ID3D12CommandQueue> commandQueue_;

	ComPtr<ID3D12Fence> fence_;
	uint64_t fenceValue_ = 0;
	HANDLE fenceEvent_ = nullptr;

};

}; // Engine
