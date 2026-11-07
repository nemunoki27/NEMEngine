#include "HeightFogGPUTests.h"
#include "GPUFilterTestFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>

// c++
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>

namespace {

	// 解析解とは別に区間を数値積分して比較する
	double IntegrateReference(const float* input, double startHeight, double baseHeight) {

		constexpr int intervals = 65536;
		double sum = 0.0;
		for (int index = 0; index < intervals; ++index) {
			const double height = startHeight + input[2] * ((index + 0.5) / intervals);
			sum += input[0] * std::exp(-input[1] * (height - baseHeight));
		}
		return sum * input[3] / intervals;
	}
}

bool NEMTests::TestHeightFogGPU() {

	using namespace Engine;
	ComPtr<IDXGIFactory6> factory;
	ComPtr<IDXGIAdapter1> adapter;
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandQueue> queue;
	const D3D12_COMMAND_QUEUE_DESC queueDescription{ D3D12_COMMAND_LIST_TYPE_DIRECT };
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))) ||
		FAILED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter))) ||
		FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device))) ||
		FAILED(device->CreateCommandQueue(&queueDescription, IID_PPV_ARGS(&queue)))) return false;
	DXGI_ADAPTER_DESC1 description{};
	adapter->GetDesc1(&description);
	std::wcout << L"HeightFog GPU: " << description.Description << L"\n";
	const auto path = RuntimePaths::GetEngineProjectRoot() / "Tests/Shaders/heightFogValidation.CS.hlsl";
	const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT };
	FilterShaderCache shaders;
	const FilterTestTexture rays{ 8, 1, formats[0], {
		0.02f, 0.0f, 0.0f, 100.0f,
		0.02f, 0.2f, 0.0f, 100.0f,
		0.02f, 0.2f, 20.0f, 100.0f,
		0.02f, 0.2f, -20.0f, 100.0f,
		0.02f, 0.2f, 0.00001f, 100.0f,
		0.02f, 0.00000001f, 20.0f, 100.0f,
		0.0f, 0.2f, 20.0f, 100.0f,
		5.0f, 0.2f, -200.0f, 250.0f
	} };
	for (const float height : { 0.0f, 12.0f, -8.0f }) {
		const std::array constants{ std::bit_cast<float>(0u), std::bit_cast<float>(8u), 2.0f, height };
		const auto output = RunFilterShader(device.Get(), queue.Get(), path, 8, 1, std::array{ rays },
			formats, constants, nullptr, nullptr, &shaders);
		for (size_t index = 0; index < 8; ++index) {
			const double expected = (std::min)(80.0, IntegrateReference(rays.pixels.data() + index * 4, height, 2.0));
			const float actual = output[0].pixels[index * 4];
			const float transmission = output[0].pixels[index * 4 + 1];
			if (!std::isfinite(actual) || std::abs(actual - expected) > 0.0001 * (1.0 + expected) ||
				!std::isfinite(transmission) || transmission < 0.0f || transmission > 1.0f) return false;
		}
	}
	// 密度が極端に高い下向きの光線と密度0でもNaNを出さない
	const FilterTestTexture extreme{ 2, 1, formats[0], { 1.0f, 100.0f, -1000.0f, 1000.0f,
		0.0f, 100.0f, -1000.0f, 1000.0f } };
	const std::array extremeConstants{ std::bit_cast<float>(0u), std::bit_cast<float>(2u), 0.0f, -1000.0f };
	const auto extremeResult = RunFilterShader(device.Get(), queue.Get(), path, 2, 1, std::array{ extreme },
		formats, extremeConstants, nullptr, nullptr, &shaders);
	if (!std::isfinite(extremeResult[0].pixels[0]) || std::abs(extremeResult[0].pixels[0] - 80.0f) > 0.0001f ||
		extremeResult[0].pixels[4] != 0.0f || extremeResult[0].pixels[5] != 1.0f) return false;

	// 各BlendModeで散乱色の重複とAlphaの変更を防ぐ
	FilterTestTexture colors{ 6, 1, formats[0], {} };
	for (size_t index = 0; index < 6; ++index) colors.pixels.insert(colors.pixels.end(), { 0.8f, 0.4f, 0.2f, 0.5f });
	const std::array constants{ std::bit_cast<float>(1u), std::bit_cast<float>(6u), 0.0f, 0.0f };
	const auto output = RunFilterShader(device.Get(), queue.Get(), path, 6, 1, std::array{ colors },
		formats, constants, nullptr, nullptr, &shaders);
	const std::array<std::array<float, 3>, 6> expected{{
		{0.3f, 0.3f, 0.35f}, {0.2f, 0.1f, 0.05f}, {0.2f, 0.1f, 0.05f},
		{0.95f, 0.85f, 0.8f}, {0.2f, 0.1f, 0.05f}, {0.25f, 0.2f, 0.2f}
	}};
	for (size_t index = 0; index < expected.size(); ++index) {
		for (size_t channel = 0; channel < 3; ++channel) {
			if (std::abs(output[0].pixels[index * 4 + channel] - expected[index][channel]) > 0.00001f) return false;
		}
		if (output[0].pixels[index * 4 + 3] != 0.5f) return false;
	}
	return true;
}
