#include "DisplayOutputState.h"
#include "DisplayOutputMetadata.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Rendering/DxObject/Debug/DxDREDDiagnostics.h>

// c++
#include <algorithm>
#include <vector>
#include <stdexcept>

using namespace Engine;

namespace {

	// Device消失時は出力形式の変更を続けない
	bool CheckOutputResult(IDXGISwapChain3* swapChain, HRESULT result) {

		if (SUCCEEDED(result)) return true;
		ComPtr<ID3D12Device> device;
		if (SUCCEEDED(swapChain->GetDevice(IID_PPV_ARGS(&device))) &&
			(result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET || FAILED(device->GetDeviceRemovedReason()))) {
			DxDREDDiagnostics::CheckHRESULT(device.Get(), result, "DisplayOutputState::Apply");
			throw std::runtime_error("表示出力の変更中にDeviceが失われました");
		}
		return false;
	}
}

//============================================================================
//	DisplayOutputState classMethods
//============================================================================
DisplayOutputSettings DisplayOutputState::ResolveSettings(const DisplayOutputSettings& requested, bool hdrEnabled) {

	DisplayOutputSettings result = requested;
	result.paperWhiteNits = std::clamp(result.paperWhiteNits, 80.0f, 1000.0f);
	result.maxLuminanceNits = std::clamp(result.maxLuminanceNits, result.paperWhiteNits, 10000.0f);
	if (!hdrEnabled) result.mode = DisplayOutputMode::SDR;
	return result;
}

DisplayOutputFormat DisplayOutputState::ResolveFormat(DisplayOutputMode mode, DXGI_FORMAT sdrFormat) {

	if (mode == DisplayOutputMode::HDR10) {
		return { DXGI_FORMAT_R10G10B10A2_UNORM, DXGI_FORMAT_R10G10B10A2_UNORM,
			DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020 };
	}
	if (mode == DisplayOutputMode::ScRGB) {
		return { DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 };
	}
	if (sdrFormat == DXGI_FORMAT_B8G8R8A8_UNORM || sdrFormat == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB) {
		return { DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB,
			DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709 };
	}
	return {};
}

bool DisplayOutputState::Refresh(HWND hwnd, IDXGIFactory* factory, const DisplayOutputSettings& requested, bool force) {

	const auto now = std::chrono::steady_clock::now();
	const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
	const bool changed = requested_ != requested || monitor_ != monitor || revision_ == 0;
	if (!force && !changed && now < nextRefresh_) return false;
	const DisplayOutputSettings previous = settings_;
	const float previousWhite = uiWhiteNits_;
	const bool previousHDR = hdrEnabled_;
	nextRefresh_ = now + std::chrono::seconds(1);
	monitor_ = monitor;
	requested_ = requested;

	// モニター移動とWindowsのHDR変更を再判定する
	hdrEnabled_ = QueryHDREnabled(factory, monitor);
	uiWhiteNits_ = hdrEnabled_ ? QueryUIWhiteNits(monitor) : 80.0f;
	if (!force && !changed && previousHDR == hdrEnabled_ && previousWhite == uiWhiteNits_) return false;
	settings_ = ResolveSettings(requested_, hdrEnabled_);
	fallback_ = requested_.mode != DisplayOutputMode::SDR && !hdrEnabled_ ?
		DisplayOutputFallback::HDRUnavailable : DisplayOutputFallback::None;
	++revision_;
	return changed || previous != settings_ || previousWhite != uiWhiteNits_ || previousHDR != hdrEnabled_ || force;
}

void DisplayOutputState::UseSDR(DisplayOutputFallback reason) {

	// 希望設定を保持したまま実際の出力を制限する
	settings_.mode = DisplayOutputMode::SDR;
	fallback_ = reason;
	++revision_;
}

void DisplayOutputState::RestoreOutput(const DisplayOutputState& previous) {

	// 同じ失敗を毎フレーム繰り返さないよう希望値は保持する
	settings_ = previous.settings_;
	fallback_ = DisplayOutputFallback::ReconfigurationFailed;
	++revision_;
}

bool DisplayOutputState::Apply(IDXGISwapChain3* swapChain) const {

	const DisplayOutputFormat format = ResolveFormat(settings_.mode);
	UINT support = 0;
	if (!CheckOutputResult(swapChain, swapChain->CheckColorSpaceSupport(format.colorSpace, &support)) ||
		!(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) ||
		!CheckOutputResult(swapChain, swapChain->SetColorSpace1(format.colorSpace))) return false;

	ComPtr<IDXGISwapChain4> swapChain4;
	if (FAILED(swapChain->QueryInterface(IID_PPV_ARGS(&swapChain4)))) return settings_.mode != DisplayOutputMode::HDR10;
	if (settings_.mode != DisplayOutputMode::HDR10) {
		return CheckOutputResult(swapChain, swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr));
	}

	// PQ出力の原色と最大輝度を通知する
	auto metadata = BuildDisplayOutputMetadata(settings_.maxLuminanceNits);
	return CheckOutputResult(swapChain, swapChain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata));
}

bool DisplayOutputState::QueryHDREnabled(IDXGIFactory* factory, HMONITOR monitor) {

	// 構成変更後の古いFactoryから表示能力を取得しない
	ComPtr<IDXGIFactory1> factory1;
	ComPtr<IDXGIFactory> refreshed;
	if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory1))) && !factory1->IsCurrent() &&
		SUCCEEDED(CreateDXGIFactory(IID_PPV_ARGS(&refreshed)))) factory = refreshed.Get();

	// 出力情報はモニターごとに取得する
	for (UINT adapterIndex = 0;; ++adapterIndex) {
		ComPtr<IDXGIAdapter> adapter;
		if (factory->EnumAdapters(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND) break;
		if (!adapter) break;
		for (UINT outputIndex = 0;; ++outputIndex) {
			ComPtr<IDXGIOutput> output;
			if (adapter->EnumOutputs(outputIndex, &output) == DXGI_ERROR_NOT_FOUND) break;
			if (!output) break;
			ComPtr<IDXGIOutput6> output6;
			DXGI_OUTPUT_DESC1 desc{};
			if (FAILED(output.As(&output6)) || FAILED(output6->GetDesc1(&desc)) || desc.Monitor != monitor) continue;
			return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
		}
	}
	return false;
}

float DisplayOutputState::QueryUIWhiteNits(HMONITOR monitor) {

	MONITORINFOEXW info{};
	info.cbSize = sizeof(info);
	if (!GetMonitorInfoW(monitor, &info)) return 200.0f;
	UINT pathCount = 0;
	UINT modeCount = 0;
	if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return 200.0f;
	std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
	std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
	if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) != ERROR_SUCCESS) {
		return 200.0f;
	}
	for (UINT index = 0; index < pathCount; ++index) {
		const auto& path = paths[index];
		DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
		source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source), path.sourceInfo.adapterId, path.sourceInfo.id };
		if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS || wcscmp(source.viewGdiDeviceName, info.szDevice) != 0) continue;
		DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
		white.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL, sizeof(white), path.targetInfo.adapterId, path.targetInfo.id };
		if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS) {
			return std::clamp(80.0f * static_cast<float>(white.SDRWhiteLevel) / 1000.0f, 80.0f, 1000.0f);
		}
	}
	return 200.0f;
}
