#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/ComPtr.h>
#include <filesystem>
#include <span>
#include <vector>
#include <cstdint>
#include <d3d12.h>

namespace NEMTests {

	// 同じ入力を複数Shaderへ渡すための画像
	struct FilterTestTexture {

		uint32_t width = 0;
		uint32_t height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		std::vector<float> pixels;
	};
	// Bufferを併用するFilterのRegister配置
	struct FilterTestLayout {

		std::span<const uint32_t> inputSlots;
		std::span<const uint32_t> structuredSlots;
		uint32_t constantRegister = 1;
		bool structuredOutput = false;
		const wchar_t* entry = L"main";
	};

	// 入力画像をGPUへ送り出力を浮動小数点で読み戻す
	std::vector<FilterTestTexture> RunFilterShader(ID3D12Device* device, ID3D12CommandQueue* queue,
		const std::filesystem::path& path, uint32_t width, uint32_t height, std::span<const FilterTestTexture> inputs,
		std::span<const DXGI_FORMAT> outputFormats, std::span<const float> constants, double* gpuMs = nullptr,
		const FilterTestLayout* layout = nullptr);
	// 指定した比較元と現在のShaderが同じ結果を返すか調べる
	bool TestGPUFilterEquivalence();
	// 重複と端数を含む一括遷移の状態と発行数を調べる
	bool CheckFilterTransitionBatch(ID3D12Device* device, ID3D12CommandQueue* queue);
}
