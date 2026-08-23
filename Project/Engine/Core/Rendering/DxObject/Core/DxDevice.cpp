#include "DxDevice.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Diagnostics/Assert.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

//============================================================================
//	DxDevice classMethods
//============================================================================
void DxDevice::Create() {

	dxgiFactory_ = nullptr;
	useAdapter_ = nullptr;
	device_ = nullptr;
	adapterName_.clear();
	dedicatedVideoMemoryBytes_ = 0;
	featureLevel_ = D3D_FEATURE_LEVEL_11_0;

	HRESULT hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory_));
	Assert::Call(SUCCEEDED(hr), "DXGI Factoryの作成に失敗しました");

	for (UINT i = 0; dxgiFactory_->EnumAdapterByGpuPreference(
		i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter_)) != DXGI_ERROR_NOT_FOUND; ++i) {

		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter_->GetDesc3(&adapterDesc);
		Assert::Call(SUCCEEDED(hr), "GPUアダプター情報の取得に失敗しました");
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

			adapterName_ = WStringToString(adapterDesc.Description);
			dedicatedVideoMemoryBytes_ = adapterDesc.DedicatedVideoMemory;
			break;
		}
		useAdapter_ = nullptr;
	}

	Assert::Call(useAdapter_ != nullptr, "利用可能なGPUアダプターが見つかりません");

	D3D_FEATURE_LEVEL featuerLevels[] = {
		D3D_FEATURE_LEVEL_12_2,
		D3D_FEATURE_LEVEL_12_1,
		D3D_FEATURE_LEVEL_12_0
	};
	const char* featuerLevelStrings[] = { "12.2", "12.1", "12.0" };

	for (size_t i = 0; i < _countof(featuerLevels); i++) {

		hr = D3D12CreateDevice(useAdapter_.Get(), featuerLevels[i], IID_PPV_ARGS(&device_));

		if (SUCCEEDED(hr)) {

			featureLevel_ = featuerLevels[i];
			break;
		}
	}

	Assert::Call(device_ != nullptr, "DirectX 12デバイスの作成に失敗しました");
}

std::string DxDevice::WStringToString(const std::wstring& wstr) {

	int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
	std::string str(size_needed - 1, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, &str[0], size_needed, NULL, NULL);
	return str;
}
