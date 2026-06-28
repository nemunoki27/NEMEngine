#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>

// directX
#include <d3d12.h>
#include <dxgi1_6.h>
// c++
#include <chrono>

//============================================================================
//	FramePresenter class
// コマンド提出からPresentまでとFPS固定の待機を担うフレーム終端処理
//============================================================================
namespace Engine {

class DxCommand;
class DxCommandQueue;

class FramePresenter {
public:
	//============================================================================
	//	public Methods
	//============================================================================

	FramePresenter() = default;
	~FramePresenter() = default;

	// 高解像度タイマー生成と参照時刻の初期化を行い参照先を保持する
	void Create(ID3D12Device* device, DxCommand* command, DxCommandQueue* commandQueue);

	// 終了処理:タイマーを破棄する
	void Finalize();

	// コマンドを提出しPresentしてGPU完了とFPS待機まで行う
	void Present(IDXGISwapChain4* swapChain);
private:
	//============================================================================
	//	private Methods
	//============================================================================

	//--------- variables ----------------------------------------------------

	ID3D12Device* device_ = nullptr;
	DxCommand* command_ = nullptr;
	DxCommandQueue* commandQueue_ = nullptr;

	// FPS待機を低CPUのsleep主体にする高解像度waitableタイマーでnullなら従来のspinへフォールバックする
	HANDLE frameTimer_ = nullptr;

	std::chrono::steady_clock::time_point reference_;

	//--------- functions ----------------------------------------------------

	// コマンドリストを閉じて提出しPresentする
	void ExecuteAndPresent(IDXGISwapChain4* swapChain);
	// 固定FPS向けにCPU側の待機/時間調整を行う
	void WaitForTargetFps();
};

}; // Engine
