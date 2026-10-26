#include "ParticleGPUResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

using namespace Engine;

void Engine::BindParticleGPUViewCompute(ID3D12GraphicsCommandList* commands, const PipelineState& pipeline,
	D3D12_GPU_VIRTUAL_ADDRESS constants, ParticleGPUInstance& instance, ParticleGPUViewResources& view,
	uint32_t input, uint32_t output, bool trail) {

	auto* keys = trail ? view.trailKeys : view.keys;
	commands->SetComputeRootSignature(pipeline.GetRootSignature());
	commands->SetPipelineState(pipeline.GetComputePipeline());
	RootBindingCommand::SetComputeCBV(commands, pipeline.FindBindingByName("ParticleGPUViewConstants", ShaderBindingKind::CBV), constants);
	const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
		RootBindingCommand::SetComputeSRV(commands, pipeline.FindBindingByName(name, ShaderBindingKind::SRV), address);
	};
	const auto uav = [&](const char* name, auto& buffer) {
		RootBindingCommand::SetComputeUAV(commands, pipeline.FindBindingByName(name, ShaderBindingKind::UAV),
			buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
	};
	srv("gGPUParticles", instance.particles.GetGPUAddress());
	srv("gGPUParents", instance.parents.GetGPUAddress());
	srv("gGPUModuleStatesRead", instance.moduleStates.GetGPUAddress());
	srv("gGPUViewKeysIn", keys[input].GetGPUAddress());
	uav("gGPUViewKeysOut", keys[output]);
	uav("gGPUHistogram", view.histogram);
	uav("gGPUViewCounts", trail ? view.trailCounts : view.counts);
	uav("gGPUHistogramOffsets", view.histogramOffsets);
	uav("gGPUIndirectArgs", view.arguments);
}
