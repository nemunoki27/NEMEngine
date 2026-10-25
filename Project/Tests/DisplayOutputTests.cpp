#include "DisplayOutputTests.h"
#include "GPUFilterTestFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/DisplayOutputState.h>
#include <Engine/Core/Rendering/Core/DisplayOutputMetadata.h>
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>

using namespace Engine;

namespace {

	// PQ出力から絶対輝度を復元する
	double DecodeNits(double value) {

		const double power = std::pow(value, 32.0 / 2523.0);
		return 10000.0 * std::pow((std::max)(power - 3424.0 / 4096.0, 0.0) /
			(2413.0 / 128.0 - 2392.0 / 128.0 * power), 16384.0 / 2610.0);
	}
}

bool NEMTests::TestDisplayOutputSettings() {

	for (const auto mode : { DisplayOutputMode::SDR, DisplayOutputMode::HDR10, DisplayOutputMode::ScRGB }) {
		DisplayOutputSettings requested{ mode, 200.0f, 1000.0f };
		const auto hdr = DisplayOutputState::ResolveSettings(requested, true);
		const auto sdr = DisplayOutputState::ResolveSettings(requested, false);
		if (hdr != requested || sdr.mode != DisplayOutputMode::SDR || requested.mode != mode ||
			sdr.paperWhiteNits != requested.paperWhiteNits || sdr.maxLuminanceNits != requested.maxLuminanceNits) return false;
	}
	const auto low = DisplayOutputState::ResolveSettings({ DisplayOutputMode::HDR10, 1.0f, 2.0f }, true);
	const auto high = DisplayOutputState::ResolveSettings({ DisplayOutputMode::ScRGB, 2000.0f, 20000.0f }, true);
	if (low.paperWhiteNits != 80.0f || low.maxLuminanceNits != 80.0f ||
		high.paperWhiteNits != 1000.0f || high.maxLuminanceNits != 10000.0f) return false;
	const auto sdr = DisplayOutputState::ResolveFormat(DisplayOutputMode::SDR);
	const auto hdr = DisplayOutputState::ResolveFormat(DisplayOutputMode::HDR10);
	const auto linear = DisplayOutputState::ResolveFormat(DisplayOutputMode::ScRGB);
	if (sdr.target != DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || hdr.target != DXGI_FORMAT_R10G10B10A2_UNORM ||
		linear.target != DXGI_FORMAT_R16G16B16A16_FLOAT) return false;

	// メタデータで最大輝度と最小輝度の単位を混同しない
	for (const float peak : { 80.0f, 1000.0f, 10000.0f }) {
		const auto metadata = BuildDisplayOutputMetadata(peak);
		if (metadata.MaxMasteringLuminance != static_cast<UINT>(peak) || metadata.MinMasteringLuminance != 50 ||
			metadata.MaxContentLightLevel != static_cast<UINT16>(peak)) return false;
	}

	// 同じ設定を毎フレーム再公開しないことを確認する
	ComPtr<IDXGIFactory> factory;
	if (FAILED(CreateDXGIFactory(IID_PPV_ARGS(&factory)))) return false;
	DisplayOutputState state;
	const DisplayOutputSettings requested{ DisplayOutputMode::HDR10, 200.0f, 1000.0f };
	if (!state.Refresh(GetDesktopWindow(), factory.Get(), requested)) return false;
	const auto revision = state.GetRevision();
	if (state.Refresh(GetDesktopWindow(), factory.Get(), requested) || state.GetRevision() != revision) return false;
	DisplayOutputState restored = state;
	restored.UseSDR(DisplayOutputFallback::ColorSpaceUnsupported);
	state.RestoreOutput(restored);
	if (state.GetSettings().mode != DisplayOutputMode::SDR ||
		state.GetFallback() != DisplayOutputFallback::ReconfigurationFailed ||
		state.Refresh(GetDesktopWindow(), factory.Get(), requested)) return false;
	std::cout << "Display output mode=" << static_cast<int>(state.GetSettings().mode)
		<< " UI white=" << state.GetUIWhiteNits() << " nits\n";
	return true;
}

bool NEMTests::TestDisplayOutputGPU() {

	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandQueue> queue;
	const D3D12_COMMAND_QUEUE_DESC desc{ D3D12_COMMAND_LIST_TYPE_DIRECT };
	if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))) ||
		FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)))) return false;
	const FilterTestTexture input{ 4, 1, DXGI_FORMAT_R32G32B32A32_FLOAT,
		{ 0.0f, 0.0f, 0.0f, 0.25f, 1.0f, 1.0f, 1.0f, 0.5f,
		  4.0f, 4.0f, 4.0f, 0.75f, 1.0f, 0.0f, 0.0f, 1.0f } };
	const std::array inputs{ input };
	const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT };
	const auto path = RuntimePaths::GetEngineProjectRoot() / "Tests/Shaders/displayOutputValidation.CS.hlsl";
	const std::array<uint32_t, 1> slots{ 0 };
	const FilterTestLayout layout{ slots, {}, 0 };
	for (const uint32_t mode : { 0u, 1u, 2u }) {
		for (const float white : { 80.0f, 200.0f, 400.0f }) {

			// 製品の基準白とUI合成済みの80nitsを同じ変換で検証する
			const std::array constants{ std::bit_cast<float>(mode), white, 1000.0f, 0.0f };
			const auto result = RunFilterShader(device.Get(), queue.Get(), path, 4, 1, inputs, formats, constants, nullptr, &layout);
			const auto& pixels = result.at(0).pixels;
			for (size_t pixel = 0; pixel < 4; ++pixel) {
				if (std::abs(pixels[pixel * 4 + 3] - input.pixels[pixel * 4 + 3]) > 0.0001f) return false;
			}
			if (mode == 0) {
				for (size_t index = 0; index < pixels.size(); ++index) {
					if (std::abs(pixels[index] - input.pixels[index]) > 0.0001f) return false;
				}
			} else if (mode == 1) {
				if (std::abs(DecodeNits(pixels[4]) - white) > 0.1 ||
					std::abs(DecodeNits(pixels[8]) - white * 4.0) > 0.5 ||
					std::abs(DecodeNits(pixels[12]) - white * 0.627404) > 0.1) return false;
			} else if (std::abs(pixels[4] * 80.0f - white) > 0.01f ||
				std::abs(pixels[8] * 80.0f - white * 4.0f) > 0.01f) return false;
		}
	}
	return true;
}
