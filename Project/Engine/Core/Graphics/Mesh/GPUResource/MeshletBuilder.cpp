#include "MeshletBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Logger/Assert.h>
#include <algorithm>

//============================================================================
//	MeshletBuilder classMethods
//============================================================================

namespace {

	Engine::Vector3 ToPosition3(const Engine::Vector4& position) {

		return Engine::Vector3(position.x, position.y, position.z);
	}

	void CalcMeshletBounds(const Engine::ImportedMeshAsset& mesh, const unsigned int* vertices,
		uint32_t vertexCount, Engine::Vector3& outCenter, float& outRadius) {

		// 頂点がないメッシュレットは安全に半径0として扱う
		if (!vertices || vertexCount == 0) {
			outCenter = Engine::Vector3::AnyInit(0.0f);
			outRadius = 0.0f;
			return;
		}

		Engine::Vector3 minPos = ToPosition3(mesh.vertices[vertices[0]].position);
		Engine::Vector3 maxPos = minPos;
		// AABBを作り、その中心をカリング用の球中心にする
		for (uint32_t i = 1; i < vertexCount; ++i) {

			const Engine::Vector3 pos = ToPosition3(mesh.vertices[vertices[i]].position);
			minPos.x = (std::min)(minPos.x, pos.x);
			minPos.y = (std::min)(minPos.y, pos.y);
			minPos.z = (std::min)(minPos.z, pos.z);
			maxPos.x = (std::max)(maxPos.x, pos.x);
			maxPos.y = (std::max)(maxPos.y, pos.y);
			maxPos.z = (std::max)(maxPos.z, pos.z);
		}

		outCenter = (minPos + maxPos) * 0.5f;
		outRadius = 0.0f;
		// AABB中心から最も遠い頂点までを球半径にする
		for (uint32_t i = 0; i < vertexCount; ++i) {

			const Engine::Vector3 pos = ToPosition3(mesh.vertices[vertices[i]].position);
			outRadius = (std::max)(outRadius, Engine::Vector3::Length(pos - outCenter));
		}
	}

	void CalcMeshletCone(const Engine::ImportedMeshAsset& mesh, const unsigned int* vertices, const unsigned char* triangles,
		uint32_t primitiveCount, Engine::Vector3& outAxis, float& outCutoff) {

		outAxis = Engine::Vector3(0.0f, 0.0f, 1.0f);
		outCutoff = -1.0f;
		if (!vertices || !triangles || primitiveCount == 0) {
			return;
		}

		std::vector<Engine::Vector3> normals{};
		normals.reserve(primitiveCount);
		Engine::Vector3 axis = Engine::Vector3::AnyInit(0.0f);
		// メッシュレット内Primitiveの法線を集める
		for (uint32_t i = 0; i < primitiveCount; ++i) {

			const uint32_t triBase = i * 3;
			const Engine::Vector3 p0 = ToPosition3(mesh.vertices[vertices[triangles[triBase + 0]]].position);
			const Engine::Vector3 p1 = ToPosition3(mesh.vertices[vertices[triangles[triBase + 1]]].position);
			const Engine::Vector3 p2 = ToPosition3(mesh.vertices[vertices[triangles[triBase + 2]]].position);

			Engine::Vector3 normal = Engine::Vector3::Cross(p1 - p0, p2 - p0);
			if (normal.Length() <= 0.00001f) {
				continue;
			}
			normal = normal.Normalize();
			normals.emplace_back(normal);
			// 平均方向をNormalConeの中心軸として使う
			axis += normal;
		}
		if (normals.empty() || axis.Length() <= 0.00001f) {
			return;
		}

		axis = axis.Normalize();
		float cutoff = 1.0f;
		// 軸から一番離れた法線との内積を閾値にする
		for (const Engine::Vector3& normal : normals) {
			cutoff = (std::min)(cutoff, Engine::Vector3::Dot(axis, normal));
		}
		outAxis = axis;
		outCutoff = cutoff;
	}
}

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

	// meshoptimizerへ渡す一時バッファを最大数で確保する
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
		// AS側カリング用にBoundsとNormalConeをメッシュレット単位で保存する
		CalcMeshletBounds(mesh, tempVertices.data() + srcMeshlet.vertex_offset,
			static_cast<uint32_t>(srcMeshlet.vertex_count), dstMeshlet.boundsCenter, dstMeshlet.boundsRadius);
		CalcMeshletCone(mesh, tempVertices.data() + srcMeshlet.vertex_offset,
			tempTriangles.data() + srcMeshlet.triangle_offset,
			static_cast<uint32_t>(srcMeshlet.triangle_count), dstMeshlet.coneAxis, dstMeshlet.coneCutoff);

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
