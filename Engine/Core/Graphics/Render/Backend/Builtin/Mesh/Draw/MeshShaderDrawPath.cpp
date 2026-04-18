#include "MeshShaderDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshDrawPathCommon.h>

//============================================================================
//	MeshShaderDrawPath classMethods
//============================================================================

bool Engine::MeshShaderDrawPath::Supports(const PipelineVariantDesc& variant) const {

	return variant.kind == PipelineVariantKind::GraphicsMesh;
}

void Engine::MeshShaderDrawPath::Setup(const MeshPathSetupContext& context, std::vector<GraphicsBindItem>& scratch) {

	const MeshPreparedBatch& prepared = *context.prepared;

	// IndexBuffer
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState, "gIndices",
		prepared.gpuMesh->indexSRV.buffer->GetResource()->GetGPUVirtualAddress(),
		prepared.gpuMesh->indexSRV.srvGPUHandle, scratch);
	// MeshletBuffers
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState, "gMeshlets",
		prepared.gpuMesh->meshletSRV.buffer ? prepared.gpuMesh->meshletSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->meshletSRV.srvGPUHandle, scratch);
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState, "gMeshletVertexIndices",
		prepared.gpuMesh->meshletVertexIndexSRV.buffer ? prepared.gpuMesh->meshletVertexIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->meshletVertexIndexSRV.srvGPUHandle, scratch);
	BackendDrawCommon::AppendGraphicsSRV(*prepared.pipelineState, "gMeshletPrimitiveIndices",
		prepared.gpuMesh->meshletPrimitiveIndexSRV.buffer ? prepared.gpuMesh->meshletPrimitiveIndexSRV.buffer->GetResource()->GetGPUVirtualAddress() : 0,
		prepared.gpuMesh->meshletPrimitiveIndexSRV.srvGPUHandle, scratch);

	// バインド
	GraphicsRootBinder binder{ *prepared.pipelineState };
	binder.Bind(context.commandList, scratch);
}

void Engine::MeshShaderDrawPath::Draw(const MeshPathDrawContext& context) {

	const auto& prepared = *context.prepared;

	const uint32_t meshletCount = prepared.gpuMesh->meshletCount;
	if (meshletCount == 0 || prepared.instanceCount == 0) {
		return;
	}

	Assert::Call(meshletCount <= 65535, "meshletCount > 65535 is not supported yet");
	Assert::Call(prepared.instanceCount <= 65535, "instanceCount > 65535 is not supported yet");

	context.commandList->DispatchMesh(meshletCount, prepared.instanceCount, 1);
}