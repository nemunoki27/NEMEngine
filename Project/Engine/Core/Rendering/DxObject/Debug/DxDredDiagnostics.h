#pragma once

// directX
#include <d3d12.h>
#include <wrl/client.h>
// c++
#include <string_view>

namespace Engine::DxDredDiagnostics {

	using Microsoft::WRL::ComPtr;

	// D3D12CreateDevice() より前に呼ぶ
	void EnableBeforeDeviceCreation();

	// Device Removed 時に一度だけ Breadcrumb / Page Fault を出力
	void DumpDeviceRemovedData(
		ID3D12Device* device,
		std::string_view operation);

	// HRESULT が Device Removed 系なら Dump して false
	bool CheckHRESULT(
		ID3D12Device* device,
		HRESULT result,
		std::string_view operation);

	// HRESULT を返さない API の後に Device 状態を確認
	bool CheckDeviceState(
		ID3D12Device* device,
		std::string_view operation);

	// 新規 Device 作成時に Dump 済みフラグを解除
	void ResetForNewDevice();
}
