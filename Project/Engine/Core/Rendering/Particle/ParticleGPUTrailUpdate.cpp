#include "ParticleGPUResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <algorithm>

using namespace Engine;

void ParticleGPUService::UpdateTrails(GraphicsCore& graphics, ParticleGPUInstance& instance,
	const PipelineState& pipeline, const PipelineState* ribbonPipeline, const ParticleGPUSimulationStep& step, bool reset) {

	auto& platform = graphics.GetDXObject();
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	instance.trails.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.trailPoints.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.trailFreeList.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.ribbonOrder.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	auto constants = MakeParticleGPUTrailConstants(step.definition->settings->trail, instance.capacity);
	constants.deltaTime = step.deltaTime;
	const auto dispatch = [&](uint32_t operation, uint32_t count) {
		constants.operation = operation;
		const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
		list->SetComputeRootSignature(pipeline.GetRootSignature());
		list->SetPipelineState(pipeline.GetComputePipeline());
		RootBindingCommand::SetComputeCBV(list, pipeline.FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
		RootBindingCommand::SetComputeSRV(list, pipeline.FindBindingByName("gGPUParents", ShaderBindingKind::SRV), instance.parents.GetGPUAddress());
		const auto uav = [&](const char* name, auto& buffer) {
			RootBindingCommand::SetComputeUAV(list, pipeline.FindBindingByName(name, ShaderBindingKind::UAV),
				buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
		};
		uav("gGPUParticles", instance.particles);
		uav("gGPUStatistics", instance.statistics);
		uav("gGPUTrails", instance.trails);
		uav("gGPUTrailPoints", instance.trailPoints);
		uav("gGPUTrailFreeList", instance.trailFreeList);
		uav("gGPURibbonOrder", instance.ribbonOrder);
		list->Dispatch((count + 255) / 256, 1, 1);
		commands.UAVBarrierAll();
	};
	if (reset) {
		dispatch(0, constants.trailCapacity);
	} else {
		if (step.deltaTime > 0) {
			dispatch(1, constants.trailCapacity);
			if (constants.mode == 0) {
				dispatch(2, instance.capacity);
				dispatch(3, constants.trailCapacity);
			} else {
				SelectRibbon(graphics, instance, *ribbonPipeline, step);
			}
		}
		dispatch(9, constants.trailCapacity);
	}
}
