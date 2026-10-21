#include "GPUFilterTestFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>

namespace {

	// 各画素に固定した値を持つ検証画像を作る
	NEMTests::FilterTestTexture MakeTexture(uint32_t width, uint32_t height, DXGI_FORMAT format,
		const std::function<std::array<float, 4>(uint32_t, uint32_t)>& fill) {

		NEMTests::FilterTestTexture result{ width, height, format };
		const uint32_t count = format == DXGI_FORMAT_R32G32B32A32_FLOAT ? 4u : format == DXGI_FORMAT_R32G32_FLOAT ? 2u : 1u;
		result.pixels.reserve(width * height * count);
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				const auto values = fill(x, y);
				result.pixels.insert(result.pixels.end(), values.begin(), values.begin() + count);
			}
		}
		return result;
	}

	std::array<float, 4> Position(uint32_t x, uint32_t y) {

		return { x * 0.01f, x > 13u ? 0.03f : 0.0f, y * 0.01f, 1.0f };
	}

	std::array<float, 4> Normal(uint32_t x, uint32_t) {

		return { x > 13u ? 0.7f : 0.5f, 1.0f, 0.5f, 1.0f };
	}

	// 空間配置と再投影の境界を同じ入力で比較する
	bool CompareAO(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& reference,
		uint32_t width, uint32_t height, uint32_t scale, bool performance = false) {

		using namespace NEMTests;
		const uint32_t sourceWidth = width * scale + 1, sourceHeight = height * scale + 1;
		const std::array inputs{
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t x, uint32_t y) {
				const float value = 0.1f + 0.8f * float((x + y * 7) % 23) / 22.0f;
				return std::array<float, 4>{ value, value, value, 1.0f };
			}),
			MakeTexture(sourceWidth, sourceHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, Position),
			MakeTexture(sourceWidth, sourceHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, Normal),
			MakeTexture(sourceWidth, sourceHeight, DXGI_FORMAT_R32G32_FLOAT, [](uint32_t x, uint32_t) {
				return std::array<float, 4>{ x % 7u == 0u ? 1.0f : 0.0f, 0, 0, 0 };
			}),
			MakeTexture(sourceWidth, sourceHeight, DXGI_FORMAT_R32_UINT, [](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ std::bit_cast<float>((x + y) % 11u == 0u ? 0u : 1u), 0, 0, 0 };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t, uint32_t) {
				return std::array<float, 4>{ 0.4f, 0.4f, 0.4f, 1.0f };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [&](uint32_t x, uint32_t y) {
				auto position = Position(uint32_t((float(x) + 0.5f) / width * sourceWidth),
					uint32_t((float(y) + 0.5f) / height * sourceHeight));
				if (x % 5u == 0u) position[2] += 10.0f;
				return position;
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, [](uint32_t, uint32_t) {
				return std::array<float, 4>{ 0.5f, 1.0f, 0, 0 };
			}) };
		const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32_FLOAT };
		const std::array constants{ 0.6f, 0.0f, 0.0f, 0.0f };
		const auto relative = "Raytracing/AmbientOcclusion/ambientOcclusionFilter.CS.hlsl";
		double beforeMs = 0.0, afterMs = 0.0;
		const auto expected = RunFilterShader(device, queue, reference / relative, width, height, inputs, formats, constants,
			performance ? &beforeMs : nullptr);
		const auto actual = RunFilterShader(device, queue, Engine::RuntimePaths::GetEngineAssetPath("Shaders/Builtin") / relative,
			width, height, inputs, formats, constants, performance ? &afterMs : nullptr);
		if (performance) std::cout << "AO Filter " << width << 'x' << height << " reference=" << beforeMs <<
			"ms optimized=" << afterMs << "ms\n";
		for (size_t image = 0; image < expected.size(); ++image) {
			for (size_t index = 0; index < expected[image].pixels.size(); ++index) {
				if (!std::isfinite(actual[image].pixels[index]) ||
					std::abs(actual[image].pixels[index] - expected[image].pixels[index]) > 0.00002f) {
					std::cerr << "AO Filter mismatch " << width << 'x' << height << " scale=" << scale <<
						" output=" << image << " index=" << index << '\n';
					return false;
				}
			}
		}
		return true;
	}

	// 影の遮蔽率と半影幅、積算回数を変更前と比較する
	bool CompareShadow(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& reference,
		uint32_t width, uint32_t height, bool temporal) {

		using namespace NEMTests;
		const auto shadow = [](uint32_t x, uint32_t y) {
			return std::array<float, 4>{float((x * 7 + y * 3) % 23) / 22.0f,
				float(((x * 37 + y * 19) % 513) * 32 + (x % 16) + 1), 0, 0};
		};
		const auto position = [=](uint32_t x, uint32_t y) {
			return std::array<float, 4>{(float(x) + 0.5f) / width * 2.0f - 1.0f, 1.0f - (float(y) + 0.5f) / height * 2.0f, 0, 1};
		};
		const auto normal = [](uint32_t, uint32_t) { return std::array<float, 4>{0.5f, 0.5f, 1.0f, 1.0f}; };
		std::array inputs{
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, normal),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, position),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, [](uint32_t, uint32_t) { return std::array<float, 4>{}; }),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, shadow),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, [](uint32_t x, uint32_t y) {
				return std::array<float, 4>{float((x + y) % 3 == 0), float((x + y) % 33), 0, 0};
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, shadow),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, position),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, normal),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, normal)};
		const std::array slots{1u, 2u, 3u, 20u, 21u, 22u, 23u, 24u, 25u};
		const std::array structured{20u, 21u, 22u};
		FilterTestLayout layout{slots, structured, 3, true, temporal ? L"temporal" : L"spatial"};
		std::array<float, 44> constants{};
		constants[0] = std::bit_cast<float>(width);
		constants[1] = std::bit_cast<float>(height);
		constants[2] = std::bit_cast<float>(1u);
		constants[4] = std::bit_cast<float>(4u);
		constants[6] = std::bit_cast<float>(2u);
		constants[8] = 0.05f;
		constants[9] = 100000.0f;
		constants[10] = 100.0f;
		constants[11] = 1.0f;
		for (size_t matrix : {12u, 28u}) for (size_t axis = 0; axis < 4; ++axis) constants[matrix + axis * 5] = 1.0f;
		std::array formats{DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT};
		const auto expected = RunFilterShader(device, queue, reference / "inlineShadowFilter.CS.hlsl",
			width, height, inputs, formats, constants, nullptr, &layout);
		for (size_t input : {3u, 5u}) {
			auto& values = inputs[input];
			std::vector<float> packed;
			for (size_t index = 0; index < values.pixels.size(); index += 2) {
				const uint32_t value = uint32_t(values.pixels[index] * 65535.0f + 0.5f) | (uint32_t(values.pixels[index + 1]) << 16u);
				packed.push_back(std::bit_cast<float>(value));
			}
			values.pixels = std::move(packed);
			values.format = DXGI_FORMAT_R32_UINT;
		}
		formats[0] = DXGI_FORMAT_R32_UINT;
		const auto actual = RunFilterShader(device, queue,
			Engine::RuntimePaths::GetEngineAssetPath("Shaders/Builtin/Lighting/inlineShadowFilter.CS.hlsl"),
			width, height, inputs, formats, constants, nullptr, &layout);
		for (size_t index = 0; index < actual[0].pixels.size(); ++index) {
			const uint32_t packed = std::bit_cast<uint32_t>(actual[0].pixels[index]);
			if (std::abs(float(packed & 65535u) / 65535.0f - expected[0].pixels[index * 2]) > 2.0f / 65535.0f ||
				float(packed >> 16u) != expected[0].pixels[index * 2 + 1]) {
				std::cerr << "Shadow packing mismatch index=" << index << " temporal=" << temporal << '\n';
				return false;
			}
		}
		return true;
	}

	// 鏡面と粗い面、強い発光、無効な反射を混ぜて比較する
	bool CompareReflection(ID3D12Device* device, ID3D12CommandQueue* queue, const std::filesystem::path& reference,
		uint32_t width, uint32_t height, float step, bool antiFirefly) {

		using namespace NEMTests;
		const std::array inputs{
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ x % 13u == 0u ? 100.0f : 0.4f, float((y + x) % 9u), 0.3f,
					(x + y) % 7u == 0u ? 0.0f : 1.0f };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT, [](uint32_t x, uint32_t) {
				return std::array<float, 4>{ 0.5f + x * 0.0001f, 0, 0, 0 };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, Normal),
			MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT, [](uint32_t x, uint32_t) {
				return std::array<float, 4>{ 1.0f + x * 0.05f, 0, 0, 0 };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, Position),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t x, uint32_t) {
				return std::array<float, 4>{ 0, x % 3u == 0u ? 0.02f : 0.7f, 0, 0 };
			}) };
		const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT };
		const std::array constants{ 64.0f, 32.0f, 8.0f, step, 4.0f, 8.0f, 0.0f, 0.0f,
			antiFirefly ? 1.0f : 0.0f, 2.0f, 1.0f, 0.0f };
		const auto relative = "Raytracing/ReflectionFilter/reflectionSpatial.CS.hlsl";
		const auto expected = RunFilterShader(device, queue, reference / relative, width, height, inputs, formats, constants);
		const auto actual = RunFilterShader(device, queue, Engine::RuntimePaths::GetEngineAssetPath("Shaders/Builtin") / relative,
			width, height, inputs, formats, constants);
		for (size_t index = 0; index < expected[0].pixels.size(); ++index) {
			if (!std::isfinite(actual[0].pixels[index]) || std::abs(actual[0].pixels[index] - expected[0].pixels[index]) >
				0.00002f * std::max(1.0f, std::abs(expected[0].pixels[index]))) return false;
		}
		return true;
	}
}

bool NEMTests::TestGPUFilterEquivalence() {

	const auto reference = Engine::Algorithm::GetEnvironmentPath(L"NEM_GPU_FILTER_REFERENCE_ROOT");
	if (reference.empty()) throw std::runtime_error("比較元Shaderのフォルダーを指定してください");
	const bool performance = !Engine::Algorithm::GetEnvironmentPath(L"NEM_GPU_FILTER_PERFORMANCE").empty();
	ComPtr<ID3D12Debug> debug;
	if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) return false;
	debug->EnableDebugLayer();
	ComPtr<ID3D12Debug1> validation;
	if (!performance && SUCCEEDED(debug.As(&validation))) validation->SetEnableGPUBasedValidation(TRUE);
	ComPtr<IDXGIFactory6> factory;
	if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)))) return false;
	ComPtr<ID3D12Device> device;
	for (UINT index = 0; ; ++index) {
		ComPtr<IDXGIAdapter1> adapter;
		if (FAILED(factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) break;
		DXGI_ADAPTER_DESC1 desc{};
		adapter->GetDesc1(&desc);
		if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
			SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) break;
	}
	if (!device) return false;
	ComPtr<ID3D12CommandQueue> queue;
	const D3D12_COMMAND_QUEUE_DESC desc{};
	if (FAILED(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)))) return false;
	if (!CheckFilterTransitionBatch(device.Get(), queue.Get())) return false;
	if (performance) {
		for (uint32_t iteration = 0; iteration < 3; ++iteration) {
			if (!CompareAO(device.Get(), queue.Get(), reference, 1920, 1080, 1, true)) return false;
		}
		return true;
	}
	for (const auto size : { std::array{ 1u, 1u }, std::array{ 17u, 13u }, std::array{ 32u, 19u } }) {
		for (bool temporal : {false, true}) {
			if (!CompareShadow(device.Get(), queue.Get(), reference.parent_path() / "Opt2-ShaderReference", size[0], size[1], temporal)) return false;
		}
		for (uint32_t scale : { 1u, 2u }) {
			if (!CompareAO(device.Get(), queue.Get(), reference, size[0], size[1], scale)) return false;
		}
		for (float step : { 1.0f, 2.0f, 4.0f }) {
			if (!CompareReflection(device.Get(), queue.Get(), reference, size[0], size[1], step, false)) return false;
		}
		if (!CompareReflection(device.Get(), queue.Get(), reference, size[0], size[1], 1.0f, true)) return false;
	}
	ComPtr<ID3D12InfoQueue> messages;
	if (FAILED(device.As(&messages))) return false;
	for (UINT64 index = 0; index < messages->GetNumStoredMessagesAllowedByRetrievalFilter(); ++index) {
		SIZE_T bytes = 0;
		messages->GetMessage(index, nullptr, &bytes);
		std::vector<std::byte> storage(bytes);
		auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
		if (SUCCEEDED(messages->GetMessage(index, message, &bytes)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
			std::cerr << message->pDescription << '\n';
			return false;
		}
	}
	return true;
}
