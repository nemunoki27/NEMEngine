#include "MeshletBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>
#include <algorithm>

//============================================================================
//	MeshletBuilder classMethods
//============================================================================

uint32_t Engine::MeshletBuilder::PackPrimitive(uint32_t i0, uint32_t i1, uint32_t i2) {

	// 10bit * 3 packing
	Assert::Call(i0 < 1024 && i1 < 1024 && i2 < 1024, "Meshlet local index overflow");
	return (i0 & 0x3FFu) | ((i1 & 0x3FFu) << 10u) | ((i2 & 0x3FFu) << 20u);
}

void Engine::MeshletBuilder::Build(ImportedMeshAsset& mesh) const {

	mesh.meshlets.clear();
	mesh.meshletVertexIndices.clear();
	mesh.meshletPrimitiveIndices.clear();
	if (mesh.vertices.empty() || mesh.indices.empty() || mesh.subMeshes.empty()) {
		return;
	}
	// サブメッシュごとにメッシュレットを構築
	for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(mesh.subMeshes.size()); ++subMeshIndex) {

		BuildSubMeshMeshlets(mesh, subMeshIndex);
	}
}

void Engine::MeshletBuilder::BuildSubMeshMeshlets(ImportedMeshAsset& mesh, uint32_t subMeshIndex) const {

	SubMeshDesc& subMesh = mesh.subMeshes[subMeshIndex];
	subMesh.meshletOffset = static_cast<uint32_t>(mesh.meshlets.size());
	subMesh.meshletCount = 0;
	if (subMesh.indexCount == 0) {
		return;
	}

	// メッシュレットの最大数
	const size_t maxMeshlets = meshopt_buildMeshletsBound(subMesh.indexCount,
		kMaxMeshletVertices, kMaxMeshletPrimitives);

	std::vector<meshopt_Meshlet> tempMeshlets(maxMeshlets);
	std::vector<unsigned int> tempVertices(maxMeshlets * kMaxMeshletVertices);
	std::vector<unsigned char> tempTriangles(maxMeshlets * kMaxMeshletPrimitives * 3);
	const float* vertexPositions = reinterpret_cast<const float*>(&mesh.vertices[0].position.x);
	// メッシュレットを構築
	const size_t builtMeshletCount = meshopt_buildMeshlets(tempMeshlets.data(),
		tempVertices.data(), tempTriangles.data(), mesh.indices.data() + subMesh.indexOffset,
		subMesh.indexCount, vertexPositions, mesh.vertices.size(),
		sizeof(MeshVertex), kMaxMeshletVertices, kMaxMeshletPrimitives, 0.0f);

	tempMeshlets.resize(builtMeshletCount);
	for (const meshopt_Meshlet& srcMeshlet : tempMeshlets) {

		// メッシュレット記述を構築
		MeshletDesc dstMeshlet{};
		dstMeshlet.vertexOffset = static_cast<uint32_t>(mesh.meshletVertexIndices.size());
		dstMeshlet.vertexCount = static_cast<uint32_t>(srcMeshlet.vertex_count);
		dstMeshlet.primitiveOffset = static_cast<uint32_t>(mesh.meshletPrimitiveIndices.size());
		dstMeshlet.primitiveCount = static_cast<uint32_t>(srcMeshlet.triangle_count);
		dstMeshlet.subMeshIndex = subMeshIndex;

		// メッシュレットの頂点インデックスを追加
		for (uint32_t v = 0; v < srcMeshlet.vertex_count; ++v) {
			mesh.meshletVertexIndices.emplace_back(
				tempVertices[srcMeshlet.vertex_offset + v]);
		}
		// メッシュレットのプリミティブインデックスを追加
		for (uint32_t t = 0; t < srcMeshlet.triangle_count; ++t) {

			const uint32_t triBase = srcMeshlet.triangle_offset + t * 3;
			const uint32_t i0 = static_cast<uint32_t>(tempTriangles[triBase + 0]);
			const uint32_t i1 = static_cast<uint32_t>(tempTriangles[triBase + 1]);
			const uint32_t i2 = static_cast<uint32_t>(tempTriangles[triBase + 2]);

			mesh.meshletPrimitiveIndices.emplace_back(PackPrimitive(i0, i1, i2));
		}
		mesh.meshlets.emplace_back(dstMeshlet);
		++subMesh.meshletCount;
	}
}