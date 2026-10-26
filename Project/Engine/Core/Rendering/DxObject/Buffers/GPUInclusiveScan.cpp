#include "GPUInclusiveScan.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <algorithm>
#include <stdexcept>

using namespace Engine;

GPUInclusiveScanPlan Engine::MakeGPUInclusiveScanPlan(uint32_t count) {

	GPUInclusiveScanPlan plan;
	uint64_t offset = 0;
	while (count != 0) {
		if (offset + count > UINT32_MAX) throw std::length_error("GPU累積和の領域が上限を超えています");
		plan.levels[plan.levelCount++] = {static_cast<uint32_t>(offset), count};
		offset += count;
		if (count <= 256) break;
		count = static_cast<uint32_t>((uint64_t(count) + 255) / 256);
	}
	plan.storageCount = static_cast<uint32_t>(offset);
	return plan;
}

void Engine::RecordGPUInclusiveScan(GraphicsCore& graphics, const PipelineState& pipeline, StructuredRWBuffer<float>& values,
	const GPUInclusiveScanPlan& plan, FrameConstantBufferAllocator& constants) {

	auto& platform = graphics.GetDXObject();
	RecordGPUInclusiveScan(platform.GetDevice(), *platform.GetDxCommand(), platform.GetResourceRetirement(),
		pipeline, values, plan, constants);
}

void Engine::RecordGPUInclusiveScan(ID3D12Device* device, DxCommand& commands, GraphicsResourceRetirement& retirement,
	const PipelineState& pipeline, StructuredRWBuffer<float>& values, const GPUInclusiveScanPlan& plan,
	FrameConstantBufferAllocator& constants) {

	if (plan.levelCount == 0) return;
	auto* list = commands.GetCommandList();
	values.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const auto dispatch = [&](uint32_t level, uint32_t operation) {
		GPUScanConstants settings;
		settings.offset = plan.levels[level].offset;
		settings.count = plan.levels[level].count;
		settings.parentOffset = level + 1 < plan.levelCount ? plan.levels[level + 1].offset : UINT32_MAX;
		settings.operation = operation;
		const uint32_t groups = static_cast<uint32_t>((uint64_t(settings.count) + 255) / 256);
		settings.groupCountX = (std::min)(groups, 65535u);
		const auto allocation = constants.AllocateAndUpload(retirement, device, settings);
		list->SetPipelineState(pipeline.GetComputePipeline());
		list->SetComputeRootSignature(pipeline.GetRootSignature());
		RootBindingCommand::SetComputeCBV(list, pipeline.FindBindingByName("GPUScanConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
		RootBindingCommand::SetComputeUAV(list, pipeline.FindBindingByName("gScanValues", ShaderBindingKind::UAV),
			values.GetGPUAddress(), values.GetUAVGPUHandle());
		list->Dispatch(settings.groupCountX, (groups + settings.groupCountX - 1) / settings.groupCountX, 1);
		commands.UAVBarrier(values.GetResource());
	};
	// 小さい階層へ累積してから各Groupの開始値を戻す
	for (uint32_t level = 0; level < plan.levelCount; ++level) dispatch(level, 0);
	for (uint32_t level = plan.levelCount - 1; level > 0; --level) dispatch(level - 1, 1);
}
