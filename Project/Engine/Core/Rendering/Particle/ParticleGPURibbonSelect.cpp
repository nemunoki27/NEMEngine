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

void ParticleGPUService::SelectRibbon(GraphicsCore& graphics, ParticleGPUInstance& instance,
	const PipelineState& pipeline, const ParticleGPUSimulationStep& step) {

	auto& platform = graphics.GetDXObject();
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	instance.particles.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	instance.statistics.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	uint32_t input = 1;
	uint32_t output = 0;
	uint32_t count = instance.capacity;
	bool first = true;
	auto constants = MakeParticleGPUTrailConstants(step.definition->settings->trail, instance.capacity);
	while (true) {
		const bool final = count <= 2048;
		constants.operation = final ? (first ? 13u : 12u) : (first ? 10u : 11u);
		constants.reserved = count;
		instance.ribbonCandidates[input].Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		instance.ribbonCandidates[output].Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
		list->SetComputeRootSignature(pipeline.GetRootSignature());
		list->SetPipelineState(pipeline.GetComputePipeline());
		RootBindingCommand::SetComputeCBV(list, pipeline.FindBindingByName("ParticleGPUTrailConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
		const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
			RootBindingCommand::SetComputeSRV(list, pipeline.FindBindingByName(name, ShaderBindingKind::SRV), address);
		};
		const auto uav = [&](const char* name, auto& buffer) {
			RootBindingCommand::SetComputeUAV(list, pipeline.FindBindingByName(name, ShaderBindingKind::UAV),
				buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
		};
		srv("gGPUParticles", instance.particles.GetGPUAddress());
		srv("gGPUParents", instance.parents.GetGPUAddress());
		srv("gGPUStatistics", instance.statistics.GetGPUAddress());
		srv("gGPURibbonCandidatesIn", instance.ribbonCandidates[input].GetGPUAddress());
		uav("gGPURibbonCandidatesOut", instance.ribbonCandidates[output]);
		uav("gGPUTrails", instance.trails);
		uav("gGPUTrailPoints", instance.trailPoints);
		uav("gGPURibbonOrder", instance.ribbonOrder);
		const uint32_t groups = (count + 2047) / 2048;
		list->Dispatch(groups, 1, 1);
		commands.UAVBarrierAll();
		if (final) break;
		count = groups * instance.pointCapacity;
		std::swap(input, output);
		first = false;
	}
	instance.particles.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.statistics.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
}
