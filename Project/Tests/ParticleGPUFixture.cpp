#include "ParticleGPUFixture.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/DxObject/Common/DxUtils.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Utility/Algorithm/PathUtility.h>

// c++
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
// directX
#include <d3d12sdklayers.h>

using namespace Engine;
using namespace NEMTests;

ParticleGPUFixture::ParticleGPUFixture() {

	ComPtr<ID3D12Debug> debug;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
	GraphicsFrameState::SetActiveCount(2);
	device.Create();
	queue.Create(device.Get());
	commands.Create(device.Get());
	descriptors.Init(device.Get(), {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE});
	descriptors.SetRetirementQueue(retirement);
	compiler.Init();
	commands.BeginFrame(0);
	commands.SetDescriptorHeaps({descriptors.GetDescriptorHeap()});
}

ParticleGPUFixture::~ParticleGPUFixture() {

	if (commands.IsRecording()) {
		commands.CloseCommandList();
		queue.ExecuteCommandList(commands.GetCommandList());
	}
	queue.SignalAndWait();
	constants.Release();
	retirement.Seal(queue.GetLastSignaledFenceValue());
	retirement.Collect(queue.GetCompletedFenceValue());
}

std::unique_ptr<PipelineState> ParticleGPUFixture::MakePipeline(const char* filename) {

	ComputePipelineDesc descriptor;
	descriptor.compute.file = Algorithm::PathToUTF8(RuntimePaths::GetEngineAssetsRoot() / "Rendering/Builtin/Particle/GPU" / filename);
	descriptor.compute.profile = "cs_6_0";
	return PipelineStateBuilder::CreateCompute(retirement, device.Get(), &compiler, descriptor);
}

void ParticleGPUFixture::Upload(ParticleGPUInstance& instance, const ParticleGPUSimulationStep& step) {

	const std::array<ParticleGPUInstruction, 1> emptyInstruction{};
	const std::array<ParticleGPUCurveSample, 1> emptyCurve{};
	instance.instructions.Upload(step.program->instructions.empty() ? std::span<const ParticleGPUInstruction>(emptyInstruction) : step.program->instructions);
	instance.curves.Upload(step.program->curves.empty() ? std::span<const ParticleGPUCurveSample>(emptyCurve) : step.program->curves);
	instance.phases.Upload(step.program->phases);
	instance.parents.Upload(step.parents);
}

void ParticleGPUFixture::Simulate(ParticleGPUInstance& instance, const PipelineState& pipeline,
	const ParticleGPUSimulationStep& step, uint32_t operation) {

	instance.particles.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.statistics.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	instance.moduleStates.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	auto values = MakeParticleGPUSimulationConstants(step);
	values.operation = operation;
	const auto allocation = constants.AllocateAndUpload(retirement, device.Get(), values);
	BindParticleGPUCompute(commands.GetCommandList(), pipeline, allocation.gpuAddress, instance);
	commands.GetCommandList()->Dispatch(operation == 3 || operation == 5 ? 1 : (instance.capacity + 255) / 256, 1, 1);
	commands.UAVBarrierAll();
}

std::vector<uint8_t> ParticleGPUFixture::Read(StructuredRWBuffer<uint32_t>& buffer, size_t bytes) {

	buffer.Transition(commands, D3D12_RESOURCE_STATE_COPY_SOURCE);
	auto result = ReadResource(buffer.GetResource(), bytes);
	buffer.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	return result;
}

std::vector<uint8_t> ParticleGPUFixture::ReadResource(ID3D12Resource* resource, size_t bytes) {

	ComPtr<ID3D12Resource> readback;
	DxUtils::CreateReadbackBufferResource(device.Get(), readback, bytes);
	commands.GetCommandList()->CopyBufferRegion(readback.Get(), 0, resource, 0, bytes);
	Finish();
	void* mapped = nullptr;
	const D3D12_RANGE range{0, bytes};
	if (FAILED(readback->Map(0, &range, &mapped))) throw std::runtime_error("粒子検証の読戻しに失敗しました");
	std::vector<uint8_t> result(bytes);
	std::memcpy(result.data(), mapped, bytes);
	const D3D12_RANGE empty{0, 0};
	readback->Unmap(0, &empty);
	return result;
}

void ParticleGPUFixture::Finish() {

	commands.CloseCommandList();
	queue.ExecuteCommandList(commands.GetCommandList());
	queue.SignalAndWait();
	retirement.Seal(queue.GetLastSignaledFenceValue());
	retirement.Collect(queue.GetCompletedFenceValue());
	frameIndex_ = (frameIndex_ + 1) % GraphicsFrameState::GetActiveCount();
	commands.BeginFrame(frameIndex_);
	commands.SetDescriptorHeaps({descriptors.GetDescriptorHeap()});
}

bool ParticleGPUFixture::CheckMessages() {

	ComPtr<ID3D12InfoQueue> information;
	if (FAILED(device.Get()->QueryInterface(IID_PPV_ARGS(&information)))) return true;
	bool valid = true;
	for (uint64_t i = 0; i < information->GetNumStoredMessages(); ++i) {
		size_t size = 0;
		information->GetMessage(i, nullptr, &size);
		std::vector<uint8_t> storage(size);
		auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
		if (SUCCEEDED(information->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
			std::cerr << message->pDescription << '\n';
			valid = false;
		}
	}
	return valid;
}

void ParticleGPUFixture::ChangeFrameCount(uint32_t count) {

	commands.CloseCommandList();
	queue.ExecuteCommandList(commands.GetCommandList());
	queue.SignalAndWait();
	auto allocator = commands.PrepareFrameContextCount(count);
	retirement.ReleaseInactiveFrameResources(count);
	GraphicsFrameState::SetActiveCount(count);
	commands.ApplyFrameContextCount(count, 0, std::move(allocator));
	frameIndex_ = 0;
	commands.BeginFrame(0);
	commands.SetDescriptorHeaps({descriptors.GetDescriptorHeap()});
}
