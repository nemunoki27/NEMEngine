#include "ParticleGPUFeedbackFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <array>

using namespace Engine;
using namespace NEMTests;

// 実行順が変わっても同じ上限で検証する
ParticleGPUFeedbackFixture::ParticleGPUFeedbackFixture(ParticleGPUFixture& target) : fixture(target) {

	auto* device = fixture.device.Get();
	auto* descriptors = &fixture.descriptors;
	routes.Init(device, descriptors); systems.Init(device, descriptors);
	previous.Init(device, descriptors); previousCounts.Init(device, descriptors);
	events.Init(device, descriptors); children.Init(device, descriptors);
	counts.Init(device, descriptors); childCounts.Init(device, descriptors); statistics.Init(device, descriptors);
	inherited.Init(device, descriptors); inherited.EnsureCapacity(5);
	phases.Init(device, descriptors); instructions.Init(device, descriptors);
	events.EnsureCapacity(4); children.EnsureCapacity(5);
	counts.EnsureCapacity(1); childCounts.EnsureCapacity(1); statistics.EnsureCapacity(8);
	values.source = identities[0];
	values.routeCount = 1;
	values.eventCapacity = 4;
	values.childCapacity = 5;
	values.systemCount = 2;
	route.target = identities[1];
	route.flags = 31;
	route.count = 3;
}

// 本番と同じ名前で全ての参照を公開する
void ParticleGPUFeedbackFixture::Bind(const PipelineState& pipeline, uint32_t operation, bool bindProgram) {

	const std::array<ParticleGPUEventRoute, 1> routeData{route};
	routes.Upload(routeData); systems.Upload(identities); previous.Upload(previousData);
	const std::array<uint32_t, 1> countData{previousCount};
	previousCounts.Upload(countData);
	std::array<ParticleGPUPhase, 1> phase{};
	phase[0].instructionCount = writesColor ? 1u : 0u;
	const std::array<ParticleGPUInstruction, 1> instruction{};
	if (bindProgram) { phases.Upload(phase); instructions.Upload(instruction); }
	events.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const auto childState = pipeline.FindBindingByName("gGPUChildren", ShaderBindingKind::SRV) ?
		D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE : D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
	children.Transition(fixture.commands, childState);
	counts.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	childCounts.Transition(fixture.commands, childState);
	statistics.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	inherited.Transition(fixture.commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	auto* commands = fixture.commands.GetCommandList();
	commands->SetComputeRootSignature(pipeline.GetRootSignature());
	commands->SetPipelineState(pipeline.GetComputePipeline());
	values.operation = operation;
	const auto allocation = fixture.constants.AllocateAndUpload(fixture.retirement, fixture.device.Get(), values);
	if (const auto* binding = pipeline.FindBindingByName("ParticleGPUEventConstants", ShaderBindingKind::CBV)) {
		RootBindingCommand::SetComputeCBV(commands, binding, allocation.gpuAddress);
	}
	const auto srv = [&](const char* name, const auto& buffer) {
		if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::SRV)) {
			RootBindingCommand::SetComputeSRV(commands, binding, buffer.GetGPUAddress(), buffer.GetGPUHandle());
		}
	};
	const auto uav = [&](const char* name, auto& buffer) {
		if (const auto* binding = pipeline.FindBindingByName(name, ShaderBindingKind::UAV)) {
			RootBindingCommand::SetComputeUAV(commands, binding, buffer.GetGPUAddress(), buffer.GetUAVGPUHandle());
		}
	};
	srv("gGPUEventRoutes", routes); srv("gGPUEventSystems", systems);
	srv("gGPUPreviousEvents", previous); srv("gGPUPreviousEventCounts", previousCounts);
	if (bindProgram) { srv("gGPUPhases", phases); srv("gGPUInstructions", instructions); }
	if (const auto* binding = pipeline.FindBindingByName("gGPUChildren", ShaderBindingKind::SRV)) {
		RootBindingCommand::SetComputeSRV(commands, binding, children.GetGPUAddress(), children.GetSRVGPUHandle());
	}
	if (const auto* binding = pipeline.FindBindingByName("gGPUChildCounts", ShaderBindingKind::SRV)) {
		RootBindingCommand::SetComputeSRV(commands, binding, childCounts.GetGPUAddress(), childCounts.GetSRVGPUHandle());
	}
	uav("gGPUEvents", events); uav("gGPUEventCounts", counts);
	uav("gGPUChildrenOut", children); uav("gGPUChildCountsOut", childCounts);
	uav("gGPUFeedbackStatistics", statistics);
	uav("gGPUInheritedStates", inherited);
}

// 一段ごとにUAVの依存関係を確定する
void ParticleGPUFeedbackFixture::Dispatch(const PipelineState& pipeline, uint32_t operation) {

	Bind(pipeline, operation);
	fixture.commands.GetCommandList()->Dispatch(1, 1, 1);
	fixture.commands.UAVBarrierAll();
}
