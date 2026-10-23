#include "ParticleGPUResources.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>

using namespace Engine;

void ParticleGPUViewResources::Init(GraphicsCore& graphics, uint32_t capacity, uint32_t phaseCount, uint32_t trailSegmentCapacity) {

	auto* device = graphics.GetDXObject().GetDevice();
	auto* descriptors = &graphics.GetSRVDescriptor();
	for (auto& buffer : keys) {
		buffer.Init(device, descriptors);
		buffer.EnsureCapacity(capacity);
	}
	counts.Init(device, descriptors);
	counts.EnsureCapacity(phaseCount * 2 + 4);
	// 粒子とTrailのソート領域をCamera内で共有する
	histogram.Init(device, descriptors);
	histogramOffsets.Init(device, descriptors);
	const uint32_t sortCapacity = (std::max)(capacity, trailSegmentCapacity);
	histogram.EnsureCapacity(((sortCapacity + 255) / 256) * 16);
	histogramOffsets.EnsureCapacity(((sortCapacity + 255) / 256) * 16);
	arguments.Init(device, descriptors);
	arguments.EnsureCapacity(phaseCount * 5);
	if (trailSegmentCapacity) {
		for (auto& buffer : trailKeys) { buffer.Init(device, descriptors); buffer.EnsureCapacity(trailSegmentCapacity); }
		trailCounts.Init(device, descriptors);
		trailCounts.EnsureCapacity(1);
		trailArguments.Init(device, descriptors);
		trailArguments.EnsureCapacity(1);
	}
}

void ParticleGPUInstance::Init(GraphicsCore& graphics, const std::shared_ptr<ParticleGPUState>& state) {

	Init(graphics.GetDXObject().GetDevice(), graphics.GetSRVDescriptor(), graphics.GetDXObject().GetResourceRetirement(), state);
}

void ParticleGPUInstance::Init(ID3D12Device* device, SRVDescriptor& descriptor,
	GraphicsResourceRetirement& retirement, const std::shared_ptr<ParticleGPUState>& state) {

	owner = state;
	auto* descriptors = &descriptor;
	capacity = state->definition->settings->emitter.maxParticles;
	phaseCount = static_cast<uint32_t>(state->definition->phases.size());
	particles.Init(device, descriptors);
	particles.EnsureCapacity(capacity);
	moduleStates.Init(device, descriptors);
	moduleStates.EnsureCapacity((std::max)(1u, capacity * state->program->stateCount));
	statistics.Init(device, descriptors);
	statistics.EnsureCapacity(4);
	instructions.Init(device, descriptors);
	curves.Init(device, descriptors);
	phases.Init(device, descriptors);
	parents.Init(device, descriptors);
	const auto& trail = state->definition->settings->trail;
	if (trail.enabled) {
		const auto constants = MakeParticleGPUTrailConstants(trail, capacity);
		trailCapacity = constants.trailCapacity;
		pointCapacity = constants.pointCapacity;
		trails.Init(device, descriptors);
		trails.EnsureCapacity(trailCapacity);
		trailPoints.Init(device, descriptors);
		trailPoints.EnsureCapacity(trailCapacity * pointCapacity);
		trailFreeList.Init(device, descriptors);
		trailFreeList.EnsureCapacity(trailCapacity + 1);
		ribbonOrder.Init(device, descriptors);
		ribbonOrder.EnsureCapacity(pointCapacity);
		if (trail.mode == ParticleTrailMode::Ribbon) {
			for (auto& buffer : ribbonCandidates) {
				buffer.Init(device, descriptors);
				buffer.EnsureCapacity(((capacity + 2047) / 2048) * pointCapacity);
			}
		}
	}
	// 読戻し待ちが重なっても描画を止めない
	for (auto& readback : readbacks) readback.buffer.CreateBuffer(retirement, device);
}

void Engine::BindParticleGPUCompute(ID3D12GraphicsCommandList* commands, const PipelineState& pipeline,
	D3D12_GPU_VIRTUAL_ADDRESS constants, ParticleGPUInstance& instance) {

	commands->SetComputeRootSignature(pipeline.GetRootSignature());
	commands->SetPipelineState(pipeline.GetComputePipeline());
	RootBindingCommand::SetComputeCBV(commands,
		pipeline.FindBindingByName("ParticleGPUSimulationConstants", ShaderBindingKind::CBV), constants);
	RootBindingCommand::SetComputeSRV(commands,
		pipeline.FindBindingByName("gGPUInstructions", ShaderBindingKind::SRV), instance.instructions.GetGPUAddress(), instance.instructions.GetGPUHandle());
	RootBindingCommand::SetComputeSRV(commands,
		pipeline.FindBindingByName("gGPUCurves", ShaderBindingKind::SRV), instance.curves.GetGPUAddress(), instance.curves.GetGPUHandle());
	RootBindingCommand::SetComputeSRV(commands,
		pipeline.FindBindingByName("gGPUPhases", ShaderBindingKind::SRV), instance.phases.GetGPUAddress(), instance.phases.GetGPUHandle());
	RootBindingCommand::SetComputeSRV(commands,
		pipeline.FindBindingByName("gGPUParents", ShaderBindingKind::SRV), instance.parents.GetGPUAddress(), instance.parents.GetGPUHandle());
	RootBindingCommand::SetComputeUAV(commands,
		pipeline.FindBindingByName("gGPUParticles", ShaderBindingKind::UAV), instance.particles.GetGPUAddress(), instance.particles.GetUAVGPUHandle());
	RootBindingCommand::SetComputeUAV(commands,
		pipeline.FindBindingByName("gGPUStatistics", ShaderBindingKind::UAV), instance.statistics.GetGPUAddress(), instance.statistics.GetUAVGPUHandle());
	RootBindingCommand::SetComputeUAV(commands,
		pipeline.FindBindingByName("gGPUModuleStates", ShaderBindingKind::UAV), instance.moduleStates.GetGPUAddress(), instance.moduleStates.GetUAVGPUHandle());
}
