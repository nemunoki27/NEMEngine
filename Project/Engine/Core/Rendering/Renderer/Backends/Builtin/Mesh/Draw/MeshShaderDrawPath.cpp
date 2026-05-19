#include "MeshShaderDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Core/Rendering/RHI/DirectX12/Common/D3D12Utils.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RootBindingCommandHelper.h>

namespace {

	void BindMeshSRV(ID3D12GraphicsCommandList6* commandList, const Engine::PipelineState& pipeline,
		std::string_view name, D3D12_GPU_VIRTUAL_ADDRESS gpuAddress, D3D12_GPU_DESCRIPTOR_HANDLE descriptor = {}) {

		// MeshShader非対応VariantではBindingが存在しない場合がある
		if (gpuAddress == 0 && descriptor.ptr == 0) {
			return;
		}
		if (const Engine::RootBindingLocation* binding = pipeline.FindBindingByName(name, Engine::ShaderBindingKind::SRV)) {

			Engine::RootBindingCommand::SetGraphicsSRV(commandList, binding, gpuAddress, descriptor);
		}
	}
}

//============================================================================
//	MeshShaderDrawPath classMethods
//============================================================================

bool Engine::MeshShaderDrawPath::Supports(const PipelineVariantDesc& variant) const {

	return variant.kind == PipelineVariantKind::GraphicsMesh;
}

void Engine::MeshShaderDrawPath::Setup(const MeshPathSetupContext& context, [[maybe_unused]] std::vector<GraphicsBindItem>& scratch) {

	const MeshPreparedBatch& prepared = *context.prepared;

	// IndexBuffer
	BindMeshSRV(context.commandList, *prepared.pipelineState, "gIndices",
		prepared.gpuMesh->indexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
		prepared.gpuMesh->indexSRV.srvGPUHandle);
	// MeshletBuffers
	// AS/MS側で必要なメッシュレット情報を直接SRVとして渡す
	BindMeshSRV(context.commandList, *prepared.pipelineState, "gMeshlets",
		prepared.gpuMesh->meshletDrawSRV.buffer ? prepared.gpuMesh->meshletDrawSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->meshletDrawSRV.srvGPUHandle);
	BindMeshSRV(context.commandList, *prepared.pipelineState, "gMeshletBounds",
		prepared.gpuMesh->meshletBoundsSRV.buffer ? prepared.gpuMesh->meshletBoundsSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->meshletBoundsSRV.srvGPUHandle);
	// 通常頂点用Indexと圧縮頂点用Indexを両方渡し、シェーダ側のフラグで選択する
	BindMeshSRV(context.commandList, *prepared.pipelineState, "gMeshletVertexIndices",
		prepared.gpuMesh->meshletVertexIndexSRV.buffer ? prepared.gpuMesh->meshletVertexIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->meshletVertexIndexSRV.srvGPUHandle);
	BindMeshSRV(context.commandList, *prepared.pipelineState, "gMeshletPrimitiveIndices",
		prepared.gpuMesh->meshletPrimitiveIndexSRV.buffer ? prepared.gpuMesh->meshletPrimitiveIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->meshletPrimitiveIndexSRV.srvGPUHandle);
	BindMeshSRV(context.commandList, *prepared.pipelineState, "gPackedMeshletVertexIndices",
		prepared.gpuMesh->packedMeshletVertexIndexSRV.buffer ? prepared.gpuMesh->packedMeshletVertexIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->packedMeshletVertexIndexSRV.srvGPUHandle);
}

void Engine::MeshShaderDrawPath::Draw(const MeshPathDrawContext& context) {

	const auto& prepared = *context.prepared;

	const uint32_t meshletCount = prepared.gpuMesh->meshletCount;
	// MeshletやInstanceがない場合はDispatchMeshしない
	if (meshletCount == 0 || prepared.instanceCount == 0) {
		return;
	}

	Assert::Call(meshletCount <= 65535, "meshletCount > 65535 is not supported yet");
	Assert::Call(prepared.instanceCount <= 65535, "instanceCount > 65535 is not supported yet");

	// X方向は32メッシュレット単位、Y方向はインスタンス単位でASを起動する
	const uint32_t meshletGroupCount = DxUtils::RoundUp(meshletCount, 32);
	context.commandList->DispatchMesh(meshletGroupCount, prepared.instanceCount, 1);
}
