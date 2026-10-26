#pragma once

//============================================================================
//	include
//============================================================================
#include "DxRWStructuredBuffer.h"
#include "FrameConstantBufferAllocator.h"

// c++
#include <array>

namespace Engine {

	class GraphicsCore;
	class PipelineState;
	class DxCommand;

	struct GPUScanLevel {

		uint32_t offset = 0;
		uint32_t count = 0;
	};

	//============================================================================
	//	GPUInclusiveScanPlan structure
	//	GPU内の累積和を階層ごとの範囲で管理する
	//============================================================================
	struct GPUInclusiveScanPlan {

		std::array<GPUScanLevel, 5> levels{};
		uint32_t levelCount = 0;
		uint32_t storageCount = 0;
	};

	struct GPUScanConstants {

		uint32_t offset = 0;
		uint32_t count = 0;
		uint32_t parentOffset = UINT32_MAX;
		uint32_t operation = 0;
		uint32_t groupCountX = 1;
		uint32_t reserved[3]{};
	};
	static_assert(sizeof(GPUScanConstants) == 32);

	// 読戻しなしで処理できる累積和の範囲を作る
	GPUInclusiveScanPlan MakeGPUInclusiveScanPlan(uint32_t count);
	// 先頭階層へ書かれた値を累積和へ置き換える
	void RecordGPUInclusiveScan(GraphicsCore& graphics, const PipelineState& pipeline, StructuredRWBuffer<float>& values,
		const GPUInclusiveScanPlan& plan, FrameConstantBufferAllocator& constants);
	// 描画先を持たない検証や転送処理でも同じ命令を記録する
	void RecordGPUInclusiveScan(ID3D12Device* device, DxCommand& commands, GraphicsResourceRetirement& retirement,
		const PipelineState& pipeline, StructuredRWBuffer<float>& values, const GPUInclusiveScanPlan& plan,
		FrameConstantBufferAllocator& constants);
}
