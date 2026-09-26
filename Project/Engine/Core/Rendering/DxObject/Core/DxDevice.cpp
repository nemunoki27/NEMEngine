#include "DxDevice.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Debug/DxDredDiagnostics.h>
#include <Engine/Core/Foundation/Utility/Algorithm/UTFConversion.h>
#include <initializer_list>
#include <stdexcept>
#include <utility>

//============================================================================
//	DxDevice classMethods
//============================================================================
void DxDevice::Create() {

	if (device_) throw std::logic_error("描画Deviceは作成済みです");
	ComPtr<IDXGIFactory7> factory;
	const HRESULT factoryResult = CreateDXGIFactory(IID_PPV_ARGS(&factory));
	if (!DxDredDiagnostics::CheckHRESULT(nullptr, factoryResult, "DxDevice::Create/Factory")) {
		throw std::runtime_error("DXGI Factoryの作成に失敗しました");
	}

	// 高性能順で最初のハードウェアAdapterを選ぶ
	ComPtr<IDXGIAdapter4> adapter;
	DXGI_ADAPTER_DESC3 adapterDesc{};
	for (UINT index = 0;; ++index) {
		const HRESULT result = factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
			IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
		if (result == DXGI_ERROR_NOT_FOUND) break;
		if (!DxDredDiagnostics::CheckHRESULT(nullptr, result, "DxDevice::Create/EnumAdapter")) {
			throw std::runtime_error("GPUアダプターの列挙に失敗しました");
		}
		if (!DxDredDiagnostics::CheckHRESULT(nullptr, adapter->GetDesc3(&adapterDesc), "DxDevice::Create/GetDesc")) {
			throw std::runtime_error("GPUアダプター情報の取得に失敗しました");
		}
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) break;
		adapter.Reset();
	}
	if (!adapter) throw std::runtime_error("利用可能なGPUアダプターが見つかりません");
	std::string adapterName = Algorithm::ConvertString(std::wstring(adapterDesc.Description));

	// 対応するFeatureLevelでDeviceを作る
	ComPtr<ID3D12Device8> device;
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
	HRESULT deviceResult = E_FAIL;
	for (D3D_FEATURE_LEVEL level : { D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0 }) {
		deviceResult = D3D12CreateDevice(adapter.Get(), level, IID_PPV_ARGS(device.ReleaseAndGetAddressOf()));
		if (SUCCEEDED(deviceResult)) {
			featureLevel = level;
			break;
		}
	}
	if (!DxDredDiagnostics::CheckHRESULT(nullptr, deviceResult, "DxDevice::Create/Device")) {
		throw std::runtime_error("DirectX 12デバイスの作成に失敗しました");
	}

	// DeviceとAdapter情報を同時に公開する
	device_ = std::move(device);
	dxgiFactory_ = std::move(factory);
	useAdapter_ = std::move(adapter);
	adapterName_ = std::move(adapterName);
	dedicatedVideoMemoryBytes_ = adapterDesc.DedicatedVideoMemory;
	featureLevel_ = featureLevel;
}
