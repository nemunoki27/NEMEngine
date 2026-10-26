#include "ParticleMeshSampling.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

// c++
#include <algorithm>
#include <stdexcept>

using namespace Engine;

void ParticleMeshSampling::Init(ID3D12Device* device, SRVDescriptor& descriptors, const ParticleMeshSamplingInput& input) {

	if (input.kind != ParticleSourceKind::MeshVertex && input.kind != ParticleSourceKind::MeshEdge &&
		input.kind != ParticleSourceKind::MeshSurface) throw std::invalid_argument("Meshの発生方式ではありません");
	if (!input.vertices || !input.indices || !input.vertexSubMeshes || !input.subMeshes || input.subMeshCount == 0)
		throw std::invalid_argument("粒子の発生元のGPUデータが揃っていません");
	if (input.indexCount % 3 != 0 || uint64_t(input.indexOffset) + input.indexCount > UINT32_MAX)
		throw std::invalid_argument("粒子の発生元の三角形範囲が不正です");
	vertexCount_ = input.vertexCount;
	indexCount_ = input.indexCount;
	kind_ = input.kind;
	count_ = input.kind == ParticleSourceKind::MeshVertex ? input.vertexCount :
		(input.kind == ParticleSourceKind::MeshEdge ? input.indexCount : input.indexCount / 3);
	if (count_ == 0) throw std::invalid_argument("粒子の発生元に頂点や三角形がありません");
	plan_ = MakeGPUInclusiveScanPlan(count_);
	distribution_.Init(device, &descriptors);
	distribution_.EnsureCapacity(plan_.storageCount);
	usedVertices_.Init(device, &descriptors);
	usedVertices_.EnsureCapacity(input.kind == ParticleSourceKind::MeshVertex ? input.vertexCount : 1u);
}

void ParticleMeshSampling::Record(ID3D12Device* device, DxCommand& commands, GraphicsResourceRetirement& retirement,
	const PipelineState& prepare, const PipelineState& scan, const ParticleMeshSamplingInput& input,
	FrameConstantBufferAllocator& constants) {

	if (input.kind != kind_ || input.vertexCount != vertexCount_ || input.indexCount != indexCount_)
		throw std::invalid_argument("粒子の発生元の構成が変更されています");
	auto* list = commands.GetCommandList();
	distribution_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	usedVertices_.Transition(commands, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	const auto dispatch = [&](uint32_t operation, uint32_t count) {
		if (count == 0) return;
		ParticleMeshSamplingConstants settings;
		settings.vertexCount = input.vertexCount;
		settings.indexOffset = input.indexOffset;
		settings.indexCount = input.indexCount;
		settings.kind = static_cast<uint32_t>(input.kind);
		settings.subMeshCount = input.subMeshCount;
		settings.elementCount = count;
		settings.operation = operation;
		const uint32_t groups = static_cast<uint32_t>((uint64_t(count) + 255) / 256);
		settings.groupCountX = (std::min)(groups, 65535u);
		const auto allocation = constants.AllocateAndUpload(retirement, device, settings);
		list->SetPipelineState(prepare.GetComputePipeline());
		list->SetComputeRootSignature(prepare.GetRootSignature());
		RootBindingCommand::SetComputeCBV(list, prepare.FindBindingByName("ParticleMeshSamplingConstants", ShaderBindingKind::CBV), allocation.gpuAddress);
		const auto srv = [&](const char* name, D3D12_GPU_VIRTUAL_ADDRESS address) {
			RootBindingCommand::SetComputeSRV(list, prepare.FindBindingByName(name, ShaderBindingKind::SRV), address);
		};
		srv("gSourceVertices", input.vertices);
		srv("gSourceIndices", input.indices);
		srv("gSourceVertexSubMeshes", input.vertexSubMeshes);
		srv("gSourceSubMeshes", input.subMeshes);
		RootBindingCommand::SetComputeUAV(list, prepare.FindBindingByName("gSourceDistribution", ShaderBindingKind::UAV),
			distribution_.GetGPUAddress(), distribution_.GetUAVGPUHandle());
		RootBindingCommand::SetComputeUAV(list, prepare.FindBindingByName("gSourceUsedVertices", ShaderBindingKind::UAV),
			usedVertices_.GetGPUAddress(), usedVertices_.GetUAVGPUHandle());
		list->Dispatch(settings.groupCountX, (groups + settings.groupCountX - 1) / settings.groupCountX, 1);
		commands.UAVBarrier(distribution_.GetResource());
		commands.UAVBarrier(usedVertices_.GetResource());
	};
	if (input.kind == ParticleSourceKind::MeshVertex) {
		// LOD0のIndexが参照する頂点だけを発生候補へ残す
		dispatch(0, input.vertexCount);
		dispatch(1, input.indexCount);
	}
	dispatch(2, count_);
	RecordGPUInclusiveScan(device, commands, retirement, scan, distribution_, plan_, constants);
	distribution_.Transition(commands, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
}
