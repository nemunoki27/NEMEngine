#include "ParticleGPUResources.h"
#include "ParticleGPUEnvironment.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>

// c++
#include <algorithm>
#include <array>

using namespace Engine;

namespace {

	void UploadProgram(ParticleGPUInstance& instance, const ParticleGPUSimulationStep& step,
		const ParticleGPUProgram& program) {

		const uint32_t frame = GraphicsFrameState::GetCurrentIndex();
		if (instance.frameDefinitions[frame] != step.definition || !instance.instructions.GetResource()) {
			// 空の定義でもShaderが参照できる最小領域を用意する
			const std::array<ParticleGPUInstruction, 1> emptyInstruction{};
			const std::array<ParticleGPUCurveSample, 1> emptyCurve{};
			instance.instructions.Upload(program.instructions.empty() ? std::span<const ParticleGPUInstruction>(emptyInstruction) : program.instructions);
			instance.curves.Upload(program.curves.empty() ? std::span<const ParticleGPUCurveSample>(emptyCurve) : program.curves);
			instance.phases.Upload(program.phases);
			instance.frameDefinitions[frame] = step.definition;
		}
		instance.parents.Upload(step.parents);
		instance.lastParents = step.parents;
		instance.uploadedDefinition = step.definition;
	}
}

void ParticleGPUService::Simulate(GraphicsCore& graphics, ParticleGPUInstance& instance,
	const PipelineState& pipeline, const PipelineState* trailPipeline, const PipelineState* ribbonPipeline, ParticleGPUState& state) {

	auto& platform = graphics.GetDXObject();
	auto& commands = *platform.GetDxCommand();
	auto* list = commands.GetCommandList();
	instance.particles.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.statistics.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.moduleStates.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const auto dispatch = [&](const ParticleGPUSimulationStep& step, uint32_t operation) {
		auto constants = MakeParticleGPUSimulationConstants(step);
		constants.operation = operation;
		if (pipeline.FindBindingByName("ParticleGPUEventConstants", ShaderBindingKind::CBV)) {
			// 子粒子を含む更新ごとに重ならない発生番号を割り当てる
			const uint64_t birth = step.sequence << 20;
			constants.birthLow = static_cast<uint32_t>(birth);
			constants.birthHigh = static_cast<uint32_t>(birth >> 32);
		}
		const auto allocation = constants_.AllocateAndUpload(platform.GetResourceRetirement(), platform.GetDevice(), constants);
		BindParticleGPUCompute(list, pipeline, allocation.gpuAddress, instance);
		instance.worldInputs.Bind(list, pipeline, instance.collisionDepth);
		instance.source.Bind(graphics, pipeline);
		if (environment_ && pipeline.FindBindingByName("ParticleGPUEventConstants", ShaderBindingKind::CBV))
			environment_->feedback.BindSource(graphics, pipeline, state, step.deltaTime > 0 || step.spawnCount > 0);
		list->Dispatch(operation == 3 || operation == 5 ? 1 : (instance.capacity + 255) / 256, 1, 1);
		commands.UAVBarrierAll();
	};
	if (instance.epoch != state.epoch) {
		ParticleGPUSimulationStep initialization;
		initialization.definition = state.definition;
		initialization.program = state.program;
		initialization.parents.resize(state.definition->phases.size());
		UploadProgram(instance, initialization, *state.program);
		dispatch(initialization, 0);
		dispatch(initialization, 5);
		if (trailPipeline) UpdateTrails(graphics, instance, *trailPipeline, ribbonPipeline, initialization, true);
		instance.epoch = state.epoch;
		state.statisticsValid = false;
	}
	for (const auto& step : state.pending) {
		const auto& program = *step.program;
		instance.lastEmitterWorld = step.emitterWorld;
		UploadProgram(instance, step, program);
		dispatch(step, step.deltaTime > 0 ? 1 : 4);
		dispatch(step, 3);
		dispatch(step, 2);
		if (environment_ && environment_->feedbackUpdate && (step.deltaTime > 0 || step.spawnCount > 0))
			environment_->feedback.ConsumeChildren(state);
		if (trailPipeline) UpdateTrails(graphics, instance, *trailPipeline, ribbonPipeline, step, false);
		state.lastSubmittedSequence = step.sequence;
	}
	if (state.pending.empty()) {
		// 一時停止中も現在のフレームスロットへ最後の親姿勢を接続する
		ParticleGPUSimulationStep retained;
		retained.definition = state.definition;
		retained.parents = instance.lastParents;
		UploadProgram(instance, retained, *state.program);
	}
	state.pending.clear();
	instance.particles.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	instance.moduleStates.Transition(commands, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
	if (instance.trailCapacity) {
		instance.trails.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		instance.trailPoints.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		instance.ribbonOrder.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	}
}
