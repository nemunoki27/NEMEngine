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
#include <limits>
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
	bool CompareAO(NEMTests::FilterShaderCache& shaderCache, ID3D12Device* device, ID3D12CommandQueue* queue,
		const std::filesystem::path& reference,
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
		std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32_FLOAT };
		const std::array constants{ 0.6f, 0.0f, 0.0f, 0.0f };
		const auto relative = "Raytracing/AmbientOcclusion/ambientOcclusionFilter.CS.hlsl";
		double beforeMs = 0.0, afterMs = 0.0;
		const auto expected = RunFilterShader(device, queue, reference / relative, width, height, inputs, formats, constants,
			performance ? &beforeMs : nullptr, nullptr, &shaderCache);
		formats[0] = DXGI_FORMAT_R32_FLOAT;
		const auto actual = RunFilterShader(device, queue, Engine::RuntimePaths::GetEngineAssetPath("Rendering/Builtin") / relative,
			width, height, inputs, formats, constants, performance ? &afterMs : nullptr, nullptr, &shaderCache);
		if (performance) std::cout << "AO Filter " << width << 'x' << height << " reference=" << beforeMs <<
			"ms optimized=" << afterMs << "ms\n";
		for (size_t image = 0; image < expected.size(); ++image) {
			for (size_t index = 0; index < actual[image].pixels.size(); ++index) {
				const size_t expectedIndex = image == 0 ? index * 4 : index;
				if (!std::isfinite(actual[image].pixels[index]) ||
					std::abs(actual[image].pixels[index] - expected[image].pixels[expectedIndex]) > 0.00002f) {
					std::cerr << "AO Filter mismatch " << width << 'x' << height << " scale=" << scale <<
						" output=" << image << " index=" << index << '\n';
					return false;
				}
			}
		}
		return true;
	}

	// 影の遮蔽率と半影幅、積算回数を変更前と比較する
	bool CompareShadow(NEMTests::FilterShaderCache& shaderCache, ID3D12Device* device, ID3D12CommandQueue* queue,
		const std::filesystem::path& reference,
		uint32_t width, uint32_t height, bool temporal, uint32_t step = 1, bool packedReference = false,
		bool performance = false, uint32_t pattern = 0) {

		using namespace NEMTests;
		const auto shadow = [=](uint32_t x, uint32_t y) {
			if (temporal && pattern != 0u) {
				const uint32_t radius = pattern == 3u ? 25u : pattern == 4u ? 512u : 24u;
				return std::array<float, 4>{ pattern == 1u ? 0.0f : 1.0f, float(radius * 32u + (x + y) % 16u + 1u), 0, 0 };
			}
			if (pattern == 3u) return std::array<float, 4>{ float((x * 7u + y * 3u) % 23u) / 22.0f, float((x + y) % 16u + 1u), 0, 0 };
			if (pattern == 4u || pattern == 5u) return std::array<float, 4>{
				pattern == 4u ? 1.0f : float((x / 8u + y / 8u) % 2u), float(((x * 37u + y * 19u) % 513u) * 32u + (x + y) % 16u + 1u), 0, 0 };
			if (pattern != 0u) return std::array<float, 4>{ pattern == 1u ? 0.0f : 1.0f, float(56u * 32u + (x + y) % 16u + 1u), 0, 0 };
			if (packedReference && (x / 8u + y / 8u) % 3u == 0u) return std::array<float, 4>{ 0.0f, 1.0f, 0, 0 };
			return std::array<float, 4>{float((x * 7 + y * 3) % 23) / 22.0f,
				float(((x * 37 + y * 19) % 513) * 32 + (x % 16) + 1), 0, 0};
		};
		const auto position = [=](uint32_t x, uint32_t y) {
			auto value = std::array<float, 4>{(float(x) + 0.5f) / width * 2.0f - 1.0f, 1.0f - (float(y) + 0.5f) / height * 2.0f,
				packedReference && x > width / 2u ? 0.03f : 0.0f, packedReference && x % 13u == 0u ? 0.0f : 1.0f};
			if (temporal && pattern == 10u && x % 7u == 0u) value[0] = std::numeric_limits<float>::quiet_NaN();
			if (temporal && pattern == 10u && x % 11u == 0u) value[1] = std::numeric_limits<float>::infinity();
			if (temporal && (pattern == 11u || pattern == 17u)) value[0] *= pattern == 11u ? 1e30f : 1e18f;
			return value;
		};
		const auto normal = [](uint32_t, uint32_t) { return std::array<float, 4>{0.5f, 0.5f, 1.0f, 1.0f}; };
		std::array inputs{
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [=](uint32_t x, uint32_t y) {
				return temporal && pattern == 9u ? std::array<float, 4>{ 0.5f, 0.5f, 0.5f, 1.0f } : normal(x, y);
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, position),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, [=](uint32_t, uint32_t) {
				return temporal && pattern == 6u ? std::array<float, 4>{ 2.0f, 2.0f, 0, 0 } : std::array<float, 4>{};
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, shadow),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, [=](uint32_t x, uint32_t y) {
				if (temporal && pattern != 0u) return std::array<float, 4>{ pattern == 1u ? 0.0f : pattern == 13u ? 0.5f : 1.0f,
					1.5f + (pattern == 14u ? float((x + y) % 3u) / 16.0f : 0.0f), 0, 0 };
				return std::array<float, 4>{float((x + y) % 3 == 0), float((x + y) % 33), 0, 0};
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, shadow),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [=](uint32_t x, uint32_t y) {
				auto value = position(x, y);
				if (temporal && pattern == 7u) value[2] += 0.1f;
				if (temporal && pattern == 15u) value[0] += 1000.0f;
				if (temporal && pattern == 16u) value[3] = 0.0f;
				return value;
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [=](uint32_t x, uint32_t y) {
				return temporal && pattern == 8u ? std::array<float, 4>{ 0.5f, 0.5f, 0.0f, 1.0f } : normal(x, y);
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, normal)};
		const std::array slots{1u, 2u, 3u, 20u, 21u, 22u, 23u, 24u, 25u};
		const std::array structured{20u, 21u, 22u};
		FilterTestLayout layout{slots, structured, 3, true, temporal ? L"temporal" : L"spatial"};
		std::array<float, 44> constants{};
		constants[0] = std::bit_cast<float>(width);
		constants[1] = std::bit_cast<float>(height);
		constants[2] = std::bit_cast<float>(1u);
		constants[4] = std::bit_cast<float>(4u);
		constants[5] = std::bit_cast<float>(temporal && pattern == 5u ? 1u : 0u);
		constants[6] = std::bit_cast<float>(step);
		constants[8] = 0.05f;
		constants[9] = 100000.0f;
		constants[10] = temporal && pattern == 12u ? 0.0f : 100.0f;
		constants[11] = 1.0f;
		for (size_t matrix : {12u, 28u}) for (size_t axis = 0; axis < 4; ++axis) constants[matrix + axis * 5] = 1.0f;
		std::array formats{DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32_FLOAT};
		double beforeMs = 0.0, afterMs = 0.0;
		std::vector<FilterTestTexture> expected;
		if (!packedReference) expected = RunFilterShader(device, queue, reference / "inlineShadowFilter.CS.hlsl",
			width, height, inputs, formats, constants, nullptr, &layout, &shaderCache);
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
		if (packedReference) expected = RunFilterShader(device, queue, reference / "inlineShadowFilter.CS.hlsl",
			width, height, inputs, formats, constants, performance ? &beforeMs : nullptr, &layout, &shaderCache);
		const auto actual = RunFilterShader(device, queue,
			Engine::RuntimePaths::GetEngineAssetPath("Rendering/Builtin/Lighting/inlineShadowFilter.CS.hlsl"),
			width, height, inputs, formats, constants, performance ? &afterMs : nullptr, &layout, &shaderCache);
		if (performance) std::cout << "Shadow Filter " << width << 'x' << height << " temporal=" << temporal << " step=" << step << " pattern=" << pattern <<
			" reference=" << beforeMs << "ms optimized=" << afterMs << "ms\n";
		for (size_t index = 0; index < actual[0].pixels.size(); ++index) {
			const uint32_t packed = std::bit_cast<uint32_t>(actual[0].pixels[index]);
			const uint32_t referenceValue = packedReference ? std::bit_cast<uint32_t>(expected[0].pixels[index]) : 0u;
			const float referenceShadow = packedReference ? float(referenceValue & 65535u) / 65535.0f : expected[0].pixels[index * 2];
			const float referenceHistory = packedReference ? float(referenceValue >> 16u) : expected[0].pixels[index * 2 + 1];
			if (std::abs(float(packed & 65535u) / 65535.0f - referenceShadow) > 2.0f / 65535.0f ||
				float(packed >> 16u) != referenceHistory) {
				std::cerr << "Shadow packing mismatch index=" << index << " temporal=" << temporal << " step=" << step <<
					" pattern=" << pattern << '\n';
				return false;
			}
		}
		return true;
	}

	// HDR境界と部分Viewportを含むFXAAの補間結果を比較する
	bool CompareFXAA(NEMTests::FilterShaderCache& shaderCache, ID3D12Device* device, ID3D12CommandQueue* queue,
		const std::filesystem::path& reference,
		uint32_t width, uint32_t height, uint32_t mode, bool performance = false) {

		using namespace NEMTests;
		const uint32_t sourceScale = mode == 3u ? 2u : 1u;
		const std::array inputs{ MakeTexture(width * sourceScale, height * sourceScale, DXGI_FORMAT_R32G32B32A32_FLOAT,
			[](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ x % 7u == 0u ? 10000.0f : float((x + y) % 11u) * 0.07f,
					y % 13u == 0u ? 30.0f : 0.3f, x > y ? 2.0f : 0.0f, float((x * 3u + y) % 17u) / 16.0f };
			}) };
		const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT };
		const std::array slots{ 0u };
		const std::array frame{ float(width), float(height), 1.0f / width, 1.0f / height };
		FilterTestLayout layout{ slots, {}, 1, false, L"main", frame };
		std::array<float, 8> constants{ 1.0f, 0.125f, 0.0312f, 0.75f };
		if (mode != 0u) {
			constants[6] = float(width);
			constants[7] = float(height);
		}
		if (mode == 2u && width > 4u && height > 4u) {
			constants[4] = 1.0f;
			constants[5] = 2.0f;
			constants[6] -= 3.0f;
			constants[7] -= 4.0f;
		}
		const auto relative = "PostProcess/FXAA/FXAA.CS.hlsl";
		double beforeMs = 0.0, afterMs = 0.0;
		const auto expected = RunFilterShader(device, queue, reference / relative,
			width, height, inputs, formats, constants, performance ? &beforeMs : nullptr, &layout, &shaderCache);
		const auto actual = RunFilterShader(device, queue, Engine::RuntimePaths::GetEngineAssetPath("Rendering/Builtin") / relative,
			width, height, inputs, formats, constants, performance ? &afterMs : nullptr, &layout, &shaderCache);
		if (performance) std::cout << "FXAA " << width << 'x' << height << " mode=" << mode <<
			" reference=" << beforeMs << "ms optimized=" << afterMs << "ms\n";
		for (uint32_t y = 0; y < height; ++y) {
			for (uint32_t x = 0; x < width; ++x) {
				if (constants[6] > 0.0f && (x < constants[4] || y < constants[5] ||
					x >= constants[4] + constants[6] || y >= constants[5] + constants[7])) continue;
				for (uint32_t component = 0; component < 4u; ++component) {
					const size_t index = (size_t(y) * width + x) * 4u + component;
					const float value = actual[0].pixels[index];
					const float expectedValue = expected[0].pixels[index];
					if (!std::isfinite(value) || std::abs(value - expectedValue) > 0.000002f * std::max(1.0f, std::abs(expectedValue))) {
						std::cerr << "FXAA mismatch " << width << 'x' << height << " mode=" << mode << " index=" << index << '\n';
						return false;
					}
				}
			}
		}
		return true;
	}

	// 無効反射と履歴棄却でも色と三種類の履歴出力が変わらないことを確かめる
	bool CompareReflectionTemporal(NEMTests::FilterShaderCache& shaderCache, ID3D12Device* device, ID3D12CommandQueue* queue,
		const std::filesystem::path& reference,
		uint32_t width, uint32_t height, uint32_t scale, uint32_t mode) {

		using namespace NEMTests;
		const uint32_t geometryWidth = width * scale + 1u, geometryHeight = height * scale + 1u;
		const auto position = [](uint32_t x, uint32_t y) {
			return std::array<float, 4>{ float(x) * 0.01f, float(y) * 0.01f, 0.0f, 1.0f };
		};
		const auto mappedPosition = [&](uint32_t x, uint32_t y) {
			return position(uint32_t((float(x) + 0.5f) / width * geometryWidth),
				uint32_t((float(y) + 0.5f) / height * geometryHeight));
		};
		const auto hit = [](uint32_t x, uint32_t y) {
			return std::array<float, 4>{ float(x) * 0.01f, float(y) * 0.01f, 5.0f, 1.0f };
		};
		const std::array inputs{
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [=](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ mode == 1u && x % 11u == 0u ? std::numeric_limits<float>::quiet_NaN() :
					x % 3u == 0u ? 10000.0f : 0.3f, float((x + y) % 7u), 0.6f,
					mode == 2u || (mode == 1u && x % 5u == 0u) ? 0.0f : 1.0f };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [=](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ mode == 1u && (x + y) % 9u == 0u ?
					std::numeric_limits<float>::infinity() : 0.4f, 30.0f, 0.7f, mode == 3u ? 0.0f : 1.0f };
			}),
			MakeTexture(geometryWidth, geometryHeight, DXGI_FORMAT_R32_FLOAT, [](uint32_t x, uint32_t) {
				return std::array<float, 4>{ x % 11u == 0u ? 1.0f : 0.5f, 0, 0, 0 };
			}),
			MakeTexture(geometryWidth, geometryHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, position),
			MakeTexture(geometryWidth, geometryHeight, DXGI_FORMAT_R32G32_FLOAT, [=](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ mode == 1u ? (x % 7u == 0u ? 1.2f : 0.25f / width) : 0.0f,
					mode == 1u && y % 5u == 0u ? -1.2f : 0.0f, 0, 0 };
			}),
			MakeTexture(geometryWidth, geometryHeight, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t, uint32_t) {
				return std::array<float, 4>{ 0.5f, 0.5f, 1.0f, 1.0f };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [&](uint32_t x, uint32_t y) {
				auto value = mappedPosition(x, y);
				if (mode == 1u && x % 7u == 1u) value[3] = 0.0f;
				if (mode == 1u && x % 7u == 2u) value[2] = 10.0f;
				return value;
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32_FLOAT, [=](uint32_t x, uint32_t) {
				return std::array<float, 4>{ 0.5f, mode == 1u && x % 7u == 3u ? 1.0f : 0.5f, 0, 0 };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, hit),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [&](uint32_t x, uint32_t y) {
				auto value = hit(x, y);
				if (mode == 1u && x % 7u == 4u) value[3] = 0.0f;
				if (mode == 1u && x % 7u == 5u) value[2] = 10.0f;
				return value;
			}) };
		const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT,
			DXGI_FORMAT_R32G32_FLOAT, DXGI_FORMAT_R32G32B32A32_FLOAT };
		const std::array slots{ 0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u };
		std::array<float, 16> frame{};
		frame[13] = 1.0f;
		frame[14] = 10.0f;
		FilterTestLayout layout{ slots, {}, 1, false, L"main", frame };
		const std::array constants{ 0.9f, 0.15f, 0.2f, 0.0f, 0.8f, 1.5f, 0.0f, 0.0f, 0.25f, 0.0f, 0.0f, 0.0f };
		const auto relative = "Raytracing/ReflectionFilter/reflectionTemporal.CS.hlsl";
		const auto expected = RunFilterShader(device, queue, reference / relative, width, height, inputs, formats, constants, nullptr, &layout, &shaderCache);
		const auto actual = RunFilterShader(device, queue, Engine::RuntimePaths::GetEngineAssetPath("Rendering/Builtin") / relative,
			width, height, inputs, formats, constants, nullptr, &layout, &shaderCache);
		for (size_t image = 0; image < expected.size(); ++image) {
			for (size_t index = 0; index < expected[image].pixels.size(); ++index) {
				const float value = actual[image].pixels[index], expectedValue = expected[image].pixels[index];
				if (!std::isfinite(value) || std::abs(value - expectedValue) > 0.000002f * std::max(1.0f, std::abs(expectedValue))) {
					std::cerr << "Reflection temporal mismatch " << width << 'x' << height << " scale=" << scale <<
						" mode=" << mode << " output=" << image << " index=" << index << '\n';
					return false;
				}
			}
		}
		return true;
	}

	// 鏡面と粗い面、強い発光、無効な反射を混ぜて比較する
	bool CompareReflection(NEMTests::FilterShaderCache& shaderCache, ID3D12Device* device, ID3D12CommandQueue* queue,
		const std::filesystem::path& reference,
		uint32_t width, uint32_t height, float step, bool antiFirefly,
		bool composite = false, uint32_t scale = 1, bool performance = false) {

		using namespace NEMTests;
		std::vector inputs{
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ x % 13u == 0u ? 100.0f : 0.4f, float((y + x) % 9u), 0.3f,
					(x + y) % 7u == 0u ? 0.0f : 1.0f };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT, [=](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ composite && (x + y) % 11u == 0u ? 1.0f : 0.5f + x * 0.0001f, 0, 0, 0 };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, Normal),
			MakeTexture(width, height, DXGI_FORMAT_R32_FLOAT, [](uint32_t x, uint32_t) {
				return std::array<float, 4>{ 1.0f + x * 0.05f, 0, 0, 0 };
			}),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, Position),
			MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t x, uint32_t) {
				return std::array<float, 4>{ 0, x % 3u == 0u ? 0.02f : 0.7f, 0, 0 };
			}) };
		if (composite) {

			for (size_t index = 3; index < inputs[0].pixels.size(); index += 4) {
				inputs[0].pixels[index] = float(index % 13u) * 0.25f - 0.5f;
			}
			inputs[3] = std::move(inputs[2]);
			inputs[2] = std::move(inputs[1]);
			inputs[1] = MakeTexture(std::max(1u, scale == 0 ? (width + 1u) / 2u : width / scale),
				std::max(1u, scale == 0 ? (height + 1u) / 2u : height / scale),
				DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t x, uint32_t y) {
					float value = x % 13u == 0u ? 10000.0f : float((x + y) % 9u);
					if ((x + y) % 17u == 0u) value = std::numeric_limits<float>::quiet_NaN();
					if ((x + y) % 19u == 0u) value = std::numeric_limits<float>::infinity();
					return std::array<float, 4>{ value, 0.4f, 0.3f, (x + y) % 7u == 0u ? 0.0f : 1.0f };
				});
			inputs[5] = MakeTexture(width, height, DXGI_FORMAT_R32G32B32A32_FLOAT, [](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ 0, (x + y) % 5u == 0u ? 1.0f : 0.7f, 0, 0 };
			});
			inputs.push_back(MakeTexture(width, height, DXGI_FORMAT_R32_UINT, [](uint32_t x, uint32_t y) {
				return std::array<float, 4>{ std::bit_cast<float>((x + y) % 11u == 0u ? 0u : 16u), 0, 0, 0 };
			}));
		}
		const std::array formats{ DXGI_FORMAT_R32G32B32A32_FLOAT };
		std::array constants{ 64.0f, 32.0f, 8.0f, step, 4.0f, 8.0f, 0.0f, 0.0f,
			antiFirefly ? 1.0f : 0.0f, 2.0f, 1.0f, 0.0f };
		if (composite) constants = { 0.85f, 0.15f };
		const auto relative = composite ? "Raytracing/ReflectionFilter/reflectionComposite.CS.hlsl" : "Raytracing/ReflectionFilter/reflectionSpatial.CS.hlsl";
		double beforeMs = 0.0, afterMs = 0.0;
		const auto expected = RunFilterShader(device, queue, reference / relative, width, height, inputs, formats, constants,
			performance ? &beforeMs : nullptr, nullptr, &shaderCache);
		const auto actual = RunFilterShader(device, queue, Engine::RuntimePaths::GetEngineAssetPath("Rendering/Builtin") / relative,
			width, height, inputs, formats, constants, performance ? &afterMs : nullptr, nullptr, &shaderCache);
		std::vector<FilterTestTexture> inPlace;
		if (composite && !performance) {
			const std::array<uint32_t, 7> slots{0, 1, 2, 3, 4, 5, 6};
			FilterTestLayout layout{.inputSlots = slots, .entry = L"mainInPlace", .inPlaceSourceSlot = 0};
			inPlace = RunFilterShader(device, queue, Engine::RuntimePaths::GetEngineAssetPath("Rendering/Builtin") / relative,
				width, height, inputs, formats, constants, nullptr, &layout, &shaderCache);
		}
		if (performance) std::cout << "Reflection Composite " << width << 'x' << height << " scale=" << scale <<
			" reference=" << beforeMs << "ms optimized=" << afterMs << "ms\n";
		for (size_t index = 0; index < expected[0].pixels.size(); ++index) {
			if (!inPlace.empty() && (!std::isfinite(inPlace[0].pixels[index]) ||
				std::abs(inPlace[0].pixels[index] - expected[0].pixels[index]) >
				0.00002f * std::max(1.0f, std::abs(expected[0].pixels[index])) ||
				(index % 4u == 3u && inPlace[0].pixels[index] != inputs[0].pixels[index]))) {
				std::cerr << "Reflection inplace mismatch " << width << 'x' << height << " scale=" << scale << " index=" << index << '\n';
				return false;
			}
			if (!std::isfinite(actual[0].pixels[index]) || std::abs(actual[0].pixels[index] - expected[0].pixels[index]) >
				0.00002f * std::max(1.0f, std::abs(expected[0].pixels[index]))) {
				std::cerr << "Reflection Filter mismatch " << width << 'x' << height << " composite=" << composite <<
					" scale=" << scale << " index=" << index << '\n';
				return false;
			}
		}
		return true;
	}
}

bool NEMTests::TestGPUFilterEquivalence() {

	FilterShaderCache shaderCache;
	const auto reference = Engine::Algorithm::GetEnvironmentPath(L"NEM_GPU_FILTER_REFERENCE_ROOT");
	const auto packedShadowReference = Engine::Algorithm::GetEnvironmentPath(L"NEM_GPU_SHADOW_FILTER_REFERENCE_ROOT");
	const auto fxaaReference = Engine::Algorithm::GetEnvironmentPath(L"NEM_GPU_FXAA_REFERENCE_ROOT");
	if (reference.empty() && packedShadowReference.empty() && fxaaReference.empty()) throw std::runtime_error("比較元Shaderのフォルダーを指定してください");
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
	if (!fxaaReference.empty()) {
		for (const auto size : { std::array{ 1u, 1u }, std::array{ 17u, 13u }, std::array{ 71u, 67u } }) {
			for (uint32_t mode : { 0u, 1u, 2u, 3u }) {
				if (!CompareFXAA(shaderCache, device.Get(), queue.Get(), fxaaReference, size[0], size[1], mode)) return false;
			}
		}
		if (performance && !CompareFXAA(shaderCache, device.Get(), queue.Get(), fxaaReference, 1904, 1041, 1, true)) return false;
		std::cout << "[GPUFilter] FXAA complete" << std::endl;
	}
	if (!packedShadowReference.empty()) {
		// 一定の半影幅と履歴の境界、非有限な幾何情報を比較する
		for (const auto size : { std::array{ 1u, 1u }, std::array{ 17u, 19u } }) {
			for (uint32_t pattern = 1u; pattern <= 17u; ++pattern) {
				if (!CompareShadow(shaderCache, device.Get(), queue.Get(), packedShadowReference,
					size[0], size[1], true, 1, true, false, pattern)) return false;
			}
		}
		for (const auto size : { std::array{ 1u, 1u }, std::array{ 17u, 13u }, std::array{ 32u, 19u }, std::array{ 71u, 67u } }) {
			if (!CompareShadow(shaderCache, device.Get(), queue.Get(), packedShadowReference, size[0], size[1], true, 1, true)) return false;
			for (uint32_t pattern : { 0u, 1u, 2u, 3u, 4u, 5u }) {
				for (uint32_t step : { 1u, 2u, 4u, 8u }) {
					if (!CompareShadow(shaderCache, device.Get(), queue.Get(), packedShadowReference, size[0], size[1], false, step, true, false, pattern)) return false;
				}
			}
		}
		if (performance) {
			for (uint32_t pattern : { 0u, 1u, 2u, 3u }) {
				if (!CompareShadow(shaderCache, device.Get(), queue.Get(), packedShadowReference,
					1904, 1041, true, 1, true, true, pattern)) return false;
			}
			for (uint32_t pattern : { 0u, 2u, 3u }) {
				for (uint32_t step : { 1u, 2u, 4u, 8u }) {
					if (!CompareShadow(shaderCache, device.Get(), queue.Get(), packedShadowReference, 1904, 1041, false, step, true, true, pattern)) return false;
				}
			}
		}
		std::cout << "[GPUFilter] Shadow complete" << std::endl;
	} else if (performance && !reference.empty()) {
		for (uint32_t iteration = 0; iteration < 3; ++iteration) {
			if (!CompareAO(shaderCache, device.Get(), queue.Get(), reference, 1920, 1080, 1, true)) return false;
		}
		std::cout << "[GPUFilter] AO complete" << std::endl;
		return true;
	}
	const std::array sizes{ std::array{ 1u, 1u }, std::array{ 17u, 13u }, std::array{ 32u, 19u } };
	if (!reference.empty() && packedShadowReference.empty()) {
		const auto shadowReference = reference.parent_path() / "Opt2-ShaderReference";
		for (const auto size : sizes) {
			if (!CompareShadow(shaderCache, device.Get(), queue.Get(), shadowReference, size[0], size[1], true)) return false;
			for (uint32_t step : { 1u, 2u, 4u, 8u }) {
				if (!CompareShadow(shaderCache, device.Get(), queue.Get(), shadowReference, size[0], size[1], false, step)) return false;
			}
		}
		std::cout << "[GPUFilter] Shadow complete" << std::endl;
	}
	if (!reference.empty()) {
		for (const auto size : sizes) {
			for (uint32_t scale : { 1u, 2u }) {
				if (!CompareAO(shaderCache, device.Get(), queue.Get(), reference, size[0], size[1], scale)) return false;
			}
		}
		std::cout << "[GPUFilter] AO complete" << std::endl;
		for (const auto size : sizes) {
			for (uint32_t scale : { 1u, 2u }) {
				for (uint32_t mode : { 0u, 1u, 2u, 3u }) {
					if (!CompareReflectionTemporal(shaderCache, device.Get(), queue.Get(), reference, size[0], size[1], scale, mode)) return false;
				}
			}
			for (float step : { 1.0f, 2.0f, 4.0f }) {
				if (!CompareReflection(shaderCache, device.Get(), queue.Get(), reference, size[0], size[1], step, false)) return false;
			}
			if (!CompareReflection(shaderCache, device.Get(), queue.Get(), reference, size[0], size[1], 1.0f, true)) return false;
		}
		for (const auto size : { std::array{1u, 1u}, std::array{7u, 9u}, std::array{8u, 8u}, std::array{9u, 9u},
			std::array{17u, 13u}, std::array{32u, 24u}, std::array{18u, 26u}, std::array{71u, 67u}, std::array{104u, 105u} }) {
			// 0は端数切り上げの半解像度、2は端数切り捨ての半解像度
			for (uint32_t scale : { 0u, 1u, 2u, 3u }) {
				if (!CompareReflection(shaderCache, device.Get(), queue.Get(), reference, size[0], size[1], 1.0f, false, true, scale)) return false;
			}
		}
		if (performance) {
			if (!CompareReflection(shaderCache, device.Get(), queue.Get(), reference, 1904, 1040, 1.0f, false, true, 2u, true)) return false;
			if (!CompareReflection(shaderCache, device.Get(), queue.Get(), reference, 1904, 1041, 1.0f, false, true, 2u, true)) return false;
			if (!CompareReflection(shaderCache, device.Get(), queue.Get(), reference, 1904, 1041, 1.0f, false, true, 0u, true)) return false;
		}
		std::cout << "[GPUFilter] Reflection complete" << std::endl;
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
