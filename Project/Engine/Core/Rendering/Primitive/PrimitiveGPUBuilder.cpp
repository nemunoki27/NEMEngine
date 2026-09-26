#include "PrimitiveGPUBuilder.h"

//============================================================================
//	include
//============================================================================
#include "PrimitiveGeometryManager.h"
#include "PrimitiveMeshGenerator.h"
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

bool Engine::PrimitiveGPUBuilder::Create(GraphicsCore& graphicsCore, SRVDescriptor* srvDescriptor,
	const PrimitiveMeshData& mesh, PrimitiveGeometry& geometry) {

	if (mesh.vertices.empty() || mesh.indices.empty()) {
		return false;
	}

	// 中立形式からMeshVertexへ詰め替える、描画とレイトレで同じ形式を共用する
	std::vector<MeshVertex> vertices;
	vertices.reserve(mesh.vertices.size());
	for (const PrimitiveMeshVertex& source : mesh.vertices) {

		MeshVertex vertex{};
		vertex.normal = source.normal;
		vertex.tangent = source.tangent;
		vertex.tangentSign = 1.0f;
		vertex.uv = source.texcoord;
		vertex.position = Vector4(source.position.x, source.position.y, source.position.z, 1.0f);
		vertices.emplace_back(vertex);
	}

	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	BufferUploadService& uploadService = graphicsCore.GetBufferUploadService();

	// すべての描画資源が揃ってから公開する
	PrimitiveGeometry candidate{};
	candidate.vertexBuffer.Create(device, uploadService, *srvDescriptor, std::span<const MeshVertex>(vertices), L"PrimitiveVertices");

	// 描画とBLASのIndexBuffer、ヒットShader用SRVを作る
	candidate.indexBuffer.Create(device, uploadService, std::span<const uint32_t>(mesh.indices),
		DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_GENERIC_READ);
	candidate.indexSRV.Create(device, uploadService, *srvDescriptor, std::span<const uint32_t>(mesh.indices), L"PrimitiveIndices");
	uploadService.SubmitBatch();

	// 完成した形状を公開する
	candidate.vertexCount = static_cast<uint32_t>(vertices.size());
	candidate.indexCount = static_cast<uint32_t>(mesh.indices.size());
	geometry = std::move(candidate);
	return true;
}
