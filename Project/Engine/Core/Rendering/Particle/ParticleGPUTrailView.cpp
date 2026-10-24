#include "ParticleGPUResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <utility>

using namespace Engine;

void ParticleGPUService::PrepareTrailView(GraphicsCore& graphics, ParticleGPUInstance& instance,
	ParticleGPUViewResources& view, const PipelineState& pipeline, const PipelineState& sortPipeline, ParticleGPUViewConstants viewConstants) {

	auto& platform = graphics.GetDXObject();
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	for (auto& buffer : view.trailKeys) buffer.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	view.trailArguments.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const auto viewAllocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), viewConstants);
	auto constants = MakeParticleGPUTrailConstants(instance.uploadedDefinition->settings->trail, instance.capacity);
	for (uint32_t operation : {0u, 1u}) {
		constants.operation = operation;
		const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
		list->SetComputeRootSignature(pipeline.GetRootSignature());
		list->SetPipelineState(pipeline.GetComputePipeline());
		RootBindingCommand::SetComputeCBV(list, pipeline.FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
		RootBindingCommand::SetComputeCBV(list, pipeline.FindBindingByName("ParticleGPUViewConstants", ShaderBindingKind::CBV), viewAllocation.gpuAddress);
		const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
			RootBindingCommand::SetComputeSRV(list, pipeline.FindBindingByName(name, ShaderBindingKind::SRV), address);
		};
		srv("gGPUTrails", instance.trails.GetGPUAddress());
		srv("gGPUTrailPoints", instance.trailPoints.GetGPUAddress());
		srv("gGPURibbonOrder", instance.ribbonOrder.GetGPUAddress());
		RootBindingCommand::SetComputeUAV(list, pipeline.FindBindingByName("gGPUTrailSegments", ShaderBindingKind::UAV),
			view.trailKeys[0].GetGPUAddress(), view.trailKeys[0].GetUAVGPUHandle());
		RootBindingCommand::SetComputeUAV(list, pipeline.FindBindingByName("gGPUTrailArguments", ShaderBindingKind::UAV),
			view.trailArguments.GetGPUAddress(), view.trailArguments.GetUAVGPUHandle());
		list->Dispatch(operation == 0 ? (viewConstants.sortEnabled ? (instance.trailCapacity * instance.pointCapacity + 255) / 256 : 1u) :
			(instance.trailCapacity + 255) / 256, 1, 1);
		commands.UAVBarrierAll();
	}
	// 軌跡の接続順を保ち、半透明の重なりだけCameraごとに並べ替える
	if (viewConstants.sortEnabled) {
		view.arguments.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		viewConstants.capacity = instance.trailCapacity * instance.pointCapacity;
		viewConstants.groupCount = (viewConstants.capacity + 255) / 256;
		view.trailCounts.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		uint32_t input = 0;
		uint32_t output = 1;
		for (uint32_t shift = 0; shift < 32; shift += 4) {
			view.trailKeys[input].Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
			view.trailKeys[output].Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
			viewConstants.radixShift = shift;
			for (uint32_t operation : {3u, 4u, 5u}) {
				viewConstants.operation = operation;
				const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), viewConstants);
				BindParticleGPUViewCompute(list, sortPipeline, allocation.gpuAddress, instance, view, input, output, true);
				list->Dispatch(operation == 4 ? 1 : viewConstants.groupCount, 1, 1);
				commands.UAVBarrierAll();
			}
			std::swap(input, output);
		}
	}
	view.trailKeys[0].Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	view.arguments.Transition(commands, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
	view.trailArguments.Transition(commands, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
}
