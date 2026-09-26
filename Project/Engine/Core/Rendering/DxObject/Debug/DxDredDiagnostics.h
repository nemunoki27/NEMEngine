#pragma once

// c++
#include <string_view>

// directX
#include <d3d12.h>

#include <wrl/client.h>

namespace Engine::DxDredDiagnostics {

	using Microsoft::WRL::ComPtr;

	// D3D12CreateDevice()より前に呼ぶ
	void EnableBeforeDeviceCreation();

	// Device Removed時に一度だけBreadcrumb / Page Faultを出力
	void DumpDeviceRemovedData(
		ID3D12Device* device,
		std::string_view operation);

	// HRESULTがDevice Removed系ならDumpしてfalse
	bool CheckHRESULT(
		ID3D12Device* device,
		HRESULT result,
		std::string_view operation);

	// HRESULTを返さないAPIの後にDevice状態を確認
	bool CheckDeviceState(
		ID3D12Device* device,
		std::string_view operation);

	// Deviceの消失を確認しながらFence完了を待つ
	bool WaitForFence(ID3D12Device* device, ID3D12Fence* fence, UINT64 expectedValue,
		HANDLE completionEvent, std::string_view operation);

	// Device状態を確認しながら表示待機イベントを待つ
	bool WaitForEvent(ID3D12Device* device, HANDLE event, std::string_view operation);

	// 新規Device作成時にDump済みフラグを解除
	void ResetForNewDevice();
}
