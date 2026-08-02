#include "FramePresenter.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Core/DxCommand.h>
#include <Engine/Core/Rendering/DxObject/Core/DxCommandQueue.h>
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Time/FrameProfiler.h>
#include <Engine/Core/Foundation/Time/FrameRateSettings.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>

// c++
#include <thread>

//============================================================================
//	FramePresenter classMethods
//============================================================================
void FramePresenter::Create(ID3D12Device* device, DxCommand* command, DxCommandQueue* commandQueue) {

	device_ = device;
	command_ = command;
	commandQueue_ = commandQueue;

	// FPS待機用の高解像度waitableタイマーを作成する、非対応環境では通常精度へ落ちさらに失敗時はspinへフォールバックする
	frameTimer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	if (!frameTimer_) {
		frameTimer_ = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
	}

	reference_ = std::chrono::steady_clock::now();
}

void FramePresenter::Finalize() {

	if (frameTimer_) {
		CloseHandle(frameTimer_);
		frameTimer_ = nullptr;
	}
}

void FramePresenter::Submit() {

	command_->CloseCommandList();
	commandQueue_->ExecuteCommandList(command_->GetCommandList());
}

void FramePresenter::Present(IDXGISwapChain4* swapChain) {

	// 提出したフレームのFence値は同じContextを再利用するときだけ待つ
	const uint64_t fenceValue = PresentAndSignal(swapChain);
	command_->SetCurrentFrameFenceValue(fenceValue);

	// FPS固定
	WaitForTargetFps();
}

uint64_t FramePresenter::PresentAndSignal(IDXGISwapChain4* swapChain) {

	// 目標フレームレートに応じてvsyncと上限解除を切り替える、0または60超はvsync上限を外す
	const uint32_t targetFps = FrameRateSettings::GetInstance().GetTargetFps();
	UINT syncInterval = 1;
	UINT presentFlags = 0;
	if (targetFps == 0 || targetFps > 60) {

		// vsyncを外す、ALLOW_TEARINGで作られていればtearing許可フラグで律速を解除する
		syncInterval = 0;
		DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
		if (SUCCEEDED(swapChain->GetDesc1(&swapChainDesc)) &&
			(swapChainDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)) {
			presentFlags = DXGI_PRESENT_ALLOW_TEARING;
		}
	}

	// GPUとOSに画面の交換を行うように通知する
	const HRESULT presentResult = swapChain->Present(syncInterval, presentFlags);
	if (!DxDredDiagnostics::CheckHRESULT(device_, presentResult, "FramePresenter::PresentAndSignal/Present")) {
		Assert::Call(false, "SwapChain Present failed.");
		return 0;
	}
	return commandQueue_->Signal();
}

void FramePresenter::WaitForTargetFps() {

	// 目標フレームレートはGraphicsメニューから設定され0は制限なし
	const uint32_t targetFps = FrameRateSettings::GetInstance().GetTargetFps();

	// 制限なしなら待機せず即座に基準時刻だけ更新する
	if (targetFps == 0) {
		reference_ = std::chrono::steady_clock::now();
		return;
	}

	// 目標フレームレートぴったりの時間
	const std::chrono::microseconds minTime(static_cast<uint64_t>(1000000.0 / static_cast<double>(targetFps)));
	// 取りこぼし防止でわずかに短い確認時間
	const std::chrono::microseconds minCheckTime(static_cast<uint64_t>(1000000.0 / (static_cast<double>(targetFps) + 4.0)));

	// 現在時間を取得する
	auto now = std::chrono::steady_clock::now();
	// 前回記録からの経過時間を取得する
	auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - reference_);

	// 目標時間よりわずかに短い時間しか経っていない場合
	if (elapsed < minCheckTime) {

		const auto waitUntil = reference_ + minTime;
		// 最後のspin余白より前は高解像度sleepでCPUを使わずに待つ
		const auto spinMargin = std::chrono::microseconds(1500);
		const auto sleepUntil = waitUntil - spinMargin;

		const auto sleepFrom = std::chrono::steady_clock::now();
		if (frameTimer_ && sleepFrom < sleepUntil) {

			// 相対指定は100ns単位の負値で渡す
			const auto sleepDuration = sleepUntil - sleepFrom;
			LARGE_INTEGER due{};
			due.QuadPart = -(std::chrono::duration_cast<std::chrono::nanoseconds>(sleepDuration).count() / 100);
			if (SetWaitableTimer(frameTimer_, &due, 0, nullptr, nullptr, FALSE)) {
				WaitForSingleObject(frameTimer_, INFINITE);
			}
		}

		// 残りの余白はyieldで詰めて目標時刻まで待つ
		while (std::chrono::steady_clock::now() < waitUntil) {
			std::this_thread::yield();
		}
	}

	// 現在の時間を記録する
	reference_ = std::chrono::steady_clock::now();
}
