#include "VertexMeshDrawPath.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Render/Backend/Builtin/Mesh/MeshDrawPathCommon.h>

//============================================================================
//	VertexMeshDrawPath classMethods
//============================================================================

bool Engine::VertexMeshDrawPath::Supports(const PipelineVariantDesc& variant) const {

	return variant.kind == PipelineVariantKind::GraphicsVertex ||
		variant.kind == PipelineVariantKind::GraphicsGeometry;
}

void Engine::VertexMeshDrawPath::Setup(const MeshPathSetupContext& context, [[maybe_unused]] std::vector<GraphicsBindItem>& scratch) {

	const auto& prepared = *context.prepared;

	context.commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	context.commandList->IASetIndexBuffer(&prepared.gpuMesh->indexBuffer.GetIndexBufferView());
}

void Engine::VertexMeshDrawPath::Draw(const MeshPathDrawContext& context) {

	const auto& prepared = *context.prepared;

	context.commandList->DrawIndexedInstanced(prepared.gpuMesh->indexCount, prepared.instanceCount, 0, 0, 0);
}