#include "PrimitiveGPUBuilder.h"

//============================================================================
//	include
//============================================================================
#include "PrimitiveGeometryManager.h"
#include "PrimitiveMeshGenerator.h"
#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>
#include <Engine/Core/Rendering/Core/RenderingCore.h>
#include <Engine/Core/Rendering/DxObject/Descriptors/DxShaderResourceView.h>

// c++
#include <limits>
#include <algorithm>

bool Engine::PrimitiveGPUBuilder::Create(GraphicsCore& graphicsCore, SRVDescriptor* srvDescriptor,
	const PrimitiveMeshData& mesh, PrimitiveGeometry& geometry) {

	if (mesh.vertices.empty() || mesh.indices.empty()) {
		return false;
	}
	if (mesh.vertices.size() > std::numeric_limits<uint32_t>::max() ||
		mesh.indices.size() > std::numeric_limits<uint32_t>::max()) {
		return false;
	}
	for (uint32_t index : mesh.indices) {
		if (index >= mesh.vertices.size()) {
			return false;
		}
	}

	std::vector<MeshVertex> vertices;
	ID3D12Device* device = graphicsCore.GetDXObject().GetDevice();
	BufferUploadService& uploadService = graphicsCore.GetBufferUploadService();

	// すべての描画資源が揃ってから公開する
	PrimitiveGeometry candidate{};
	ConvertVertices(mesh, vertices, candidate.planarUVBounds);
	MeshBoundsUtility::Calculate(vertices, {}, candidate.boundsCenter, candidate.boundsRadius);
	candidate.vertexBuffer.Create(device, uploadService, *srvDescriptor, std::span<const MeshVertex>(vertices), L"PrimitiveVertices");

	CreateIndices(graphicsCore, *srvDescriptor, mesh, candidate);

	// 完成した形状を公開する
	candidate.vertexCount = static_cast<uint32_t>(vertices.size());
	candidate.indexCount = static_cast<uint32_t>(mesh.indices.size());
	geometry = std::move(candidate);
	return true;
}

void Engine::PrimitiveGPUBuilder::ConvertVertices(const PrimitiveMeshData& mesh, std::vector<MeshVertex>& vertices, Vector4& planarUVBounds) {

	// 中立形式からMeshVertexへ詰め替える、描画とレイトレで同じ形式を共用する
	vertices.clear();
	vertices.reserve(mesh.vertices.size());
	Vector2 minimum{mesh.vertices.front().position.x, mesh.vertices.front().position.y};
	Vector2 maximum = minimum;
	for (const PrimitiveMeshVertex& source : mesh.vertices) {

		MeshVertex vertex{};
		vertex.normal = source.normal;
		vertex.tangent = source.tangent;
		vertex.tangentSign = source.tangentSign;
		vertex.uv = source.texcoord;
		vertex.position = Vector4(source.position.x, source.position.y, source.position.z, 1.0f);
		minimum.x = std::min(minimum.x, source.position.x);
		minimum.y = std::min(minimum.y, source.position.y);
		maximum.x = std::max(maximum.x, source.position.x);
		maximum.y = std::max(maximum.y, source.position.y);
		vertices.emplace_back(vertex);
	}

	planarUVBounds = {minimum.x, minimum.y, 1.0f / std::max(maximum.x - minimum.x, 0.00001f),
		1.0f / std::max(maximum.y - minimum.y, 0.00001f)};
}

void Engine::PrimitiveGPUBuilder::CreateIndices(GraphicsCore& graphicsCore, SRVDescriptor& descriptor,
	const PrimitiveMeshData& mesh, PrimitiveGeometry& geometry) {

	auto* device = graphicsCore.GetDXObject().GetDevice();
	auto& uploadService = graphicsCore.GetBufferUploadService();
	// 描画とBLASのIndexBuffer、ヒットShader用SRVを作る
	geometry.indexSRV.Create(device, uploadService, descriptor, std::span<const uint32_t>(mesh.indices), L"PrimitiveIndices");
	geometry.indexBuffer.Share32BitResource(uploadService.GetResourceRetirement(), geometry.indexSRV.buffer->GetResource(),
		static_cast<uint32_t>(mesh.indices.size()));
	uploadService.SubmitBatch();
}
