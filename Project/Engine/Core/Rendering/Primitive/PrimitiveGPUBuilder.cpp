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

	// 頂点はSRVで描画、BLASでも同じ本体を読む
	geometry.vertexBuffer.buffer = std::make_unique<DxImmutableStructuredBuffer<MeshVertex>>();
	geometry.vertexBuffer.buffer->Create(device, uploadService, std::span<const MeshVertex>(vertices.data(), vertices.size()));
	const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = geometry.vertexBuffer.buffer->GetSRVDesc();
	srvDescriptor->CreateSRV(geometry.vertexBuffer.srvIndex, geometry.vertexBuffer.buffer->GetResource(), srvDesc);
	geometry.vertexBuffer.srvGPUHandle = srvDescriptor->GetGPUHandle(geometry.vertexBuffer.srvIndex);

	// インデックスは描画とBLASでインデックスバッファ、レイトレのヒットシェーダーではSRVで読む
	geometry.indexBuffer.Create(device, uploadService,
		std::span<const uint32_t>(mesh.indices.data(), mesh.indices.size()),
		DXGI_FORMAT_R32_UINT, D3D12_RESOURCE_STATE_GENERIC_READ);

	geometry.indexSRV.buffer = std::make_unique<DxImmutableStructuredBuffer<uint32_t>>();
	geometry.indexSRV.buffer->Create(device, uploadService, std::span<const uint32_t>(mesh.indices.data(), mesh.indices.size()));
	const D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = geometry.indexSRV.buffer->GetSRVDesc();
	srvDescriptor->CreateSRV(geometry.indexSRV.srvIndex, geometry.indexSRV.buffer->GetResource(), indexSrvDesc);
	geometry.indexSRV.srvGPUHandle = srvDescriptor->GetGPUHandle(geometry.indexSRV.srvIndex);
	uploadService.SubmitBatch();

	if (!geometry.vertexBuffer.buffer->GetResource() || !geometry.indexBuffer.IsCreatedResource() ||
		!geometry.indexSRV.buffer->GetResource()) {
		geometry.vertexBuffer.Release(srvDescriptor);
		geometry.indexSRV.Release(srvDescriptor);
		return false;
	}

	geometry.vertexCount = static_cast<uint32_t>(vertices.size());
	geometry.indexCount = static_cast<uint32_t>(mesh.indices.size());
	return true;
}
