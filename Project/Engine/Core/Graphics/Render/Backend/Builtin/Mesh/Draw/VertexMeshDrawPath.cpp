#include "VertexMeshDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/GraphicsCore.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshBatchResources.h>
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshDrawPathCommon.h>
#include <Engine/Logger/Assert.h>

//============================================================================
//	VertexMeshDrawPath classMethods
//============================================================================

bool Engine::VertexMeshDrawPath::Supports(const PipelineVariantDesc& variant) const {

	return variant.kind == PipelineVariantKind::GraphicsVertex ||
		variant.kind == PipelineVariantKind::GraphicsGeometry;
}

void Engine::VertexMeshDrawPath::EnsureCommandSignature(ID3D12Device* device) {

	if (commandSignature_) {
		return;
	}

	D3D12_INDIRECT_ARGUMENT_DESC argumentDesc{};
	argumentDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

	D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
	signatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
	signatureDesc.NumArgumentDescs = 1;
	signatureDesc.pArgumentDescs = &argumentDesc;

	HRESULT hr = device->CreateCommandSignature(&signatureDesc, nullptr, IID_PPV_ARGS(&commandSignature_));
	Assert::Call(SUCCEEDED(hr), "CreateCommandSignature failed");
}

void Engine::VertexMeshDrawPath::Setup(const MeshPathSetupContext& context, [[maybe_unused]] std::vector<GraphicsBindItem>& scratch) {

	const auto& prepared = *context.prepared;

	context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.commandList->IASetIndexBuffer(&prepared.gpuMesh->indexBuffer.GetIndexBufferView());
}

void Engine::VertexMeshDrawPath::Draw(const MeshPathDrawContext& context) {

	const auto& prepared = *context.prepared;

	EnsureCommandSignature(context.graphicsCore->GetDXObject().GetDevice());
	prepared.resources->UpdateIndexedIndirectArgs(prepared.gpuMesh->indexCount, prepared.instanceCount);

	context.commandList->ExecuteIndirect(commandSignature_.Get(), 1,
		prepared.resources->GetIndexedIndirectArgsResource(), 0, nullptr, 0);
}
