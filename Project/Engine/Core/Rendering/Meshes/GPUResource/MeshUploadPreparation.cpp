#include "MeshUploadPreparation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Utility/MeshBoundsUtility.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
	// BLASのPrimitiveIndex()と一致する並びでサブメッシュインデックスを並べる
	std::vector<uint32_t> BuildPrimitiveSubMeshTable(const std::vector<Engine::SubMeshDesc>& subMeshes,
		uint32_t indexCount) {

		uint32_t primitiveCount = indexCount / 3;
		std::vector<uint32_t> table(primitiveCount, 0);
		for (uint32_t subMeshIndex = 0; subMeshIndex < static_cast<uint32_t>(subMeshes.size()); ++subMeshIndex) {

			const Engine::SubMeshDesc& subMesh = subMeshes[subMeshIndex];

			// サブメッシュのプリミティブ範囲を求める
			uint32_t firstPrimitive = subMesh.indexOffset / 3;
			uint32_t subMeshPrimitiveCount = subMesh.indexCount / 3;
			for (uint32_t primitiveOffset = 0; primitiveOffset < subMeshPrimitiveCount; ++primitiveOffset) {

				const uint32_t primitiveIndex = firstPrimitive + primitiveOffset;
				if (primitiveIndex < table.size()) {

					table[primitiveIndex] = subMeshIndex;
				}
			}
		}
		return table;
	}
	int16_t QuantizeSnorm16(float value) {

		// 法線のOct成分を16bit符号付き正規化値へ丸める
		value = (std::clamp)(value, -1.0f, 1.0f);
		return static_cast<int16_t>(std::round(value * 32767.0f));
	}

	uint32_t EncodeOctNormal(const Engine::Vector3& normal) {

		// 3成分法線を2成分のOctahedral表現へ変換する
		Engine::Vector3 n = normal.Normalize();
		const float length = std::abs(n.x) + std::abs(n.y) + std::abs(n.z);
		if (length <= 0.00001f) {
			return 0;
		}

		float x = n.x / length;
		float y = n.y / length;
		if (n.z < 0.0f) {

			// 裏面側の半球を2D平面へ折り返す
			const float oldX = x;
			x = (1.0f - std::abs(y)) * (oldX >= 0.0f ? 1.0f : -1.0f);
			y = (1.0f - std::abs(oldX)) * (y >= 0.0f ? 1.0f : -1.0f);
		}

		const uint16_t packedX = static_cast<uint16_t>(QuantizeSnorm16(x));
		const uint16_t packedY = static_cast<uint16_t>(QuantizeSnorm16(y));
		return static_cast<uint32_t>(packedX) | (static_cast<uint32_t>(packedY) << 16);
	}

	std::vector<Engine::MeshPackedVertex> BuildPackedVertices(const std::vector<Engine::MeshVertex>& vertices) {

		// MeshShaderで読む法線・接線を圧縮した頂点配列を作る
		std::vector<Engine::MeshPackedVertex> packed{};
		packed.reserve(vertices.size());
		for (const Engine::MeshVertex& vertex : vertices) {

			Engine::MeshPackedVertex dst{};
			dst.normalOct = EncodeOctNormal(vertex.normal);
			dst.tangentOct = EncodeOctNormal(vertex.tangent);
			// 接線の利き手は圧縮せずそのまま保持する
			dst.tangentSign = vertex.tangentSign;
			dst.uv = vertex.uv;
			dst.position = vertex.position;
			packed.emplace_back(dst);
		}
		return packed;
	}

	bool CanPackMeshletVertexIndices(const std::vector<uint32_t>& indices) {

		// 16bitに収まる場合だけ2要素/uint32_tへ圧縮する
		for (uint32_t index : indices) {
			if (0xFFFFu < index) {
				return false;
			}
		}
		return true;
	}

	std::vector<uint32_t> BuildPackedMeshletVertexIndices(const std::vector<uint32_t>& indices) {

		// 16bitのメッシュレット頂点Indexを2つずつuint32_tへ詰める
		std::vector<uint32_t> packed((indices.size() + 1) / 2, 0);
		for (size_t i = 0; i < indices.size(); ++i) {

			const uint32_t value = indices[i] & 0xFFFFu;
			if ((i & 1u) == 0) {
				packed[i >> 1] |= value;
			} else {
				packed[i >> 1] |= value << 16;
			}
		}
		return packed;
	}

	std::vector<uint16_t> BuildIndex16(const std::vector<uint32_t>& indices) {

		// IBV用に32bit Indexを16bitへ変換する
		std::vector<uint16_t> packed{};
		packed.reserve(indices.size());
		for (uint32_t index : indices) {
			packed.emplace_back(static_cast<uint16_t>(index));
		}
		return packed;
	}

	std::vector<Engine::MeshletDrawDesc> BuildMeshletDrawDescs(const std::vector<Engine::MeshletDesc>& meshlets) {

		// MSが読む範囲情報だけを抜き出す
		std::vector<Engine::MeshletDrawDesc> result{};
		result.reserve(meshlets.size());
		for (const Engine::MeshletDesc& meshlet : meshlets) {

			Engine::MeshletDrawDesc desc{};
			desc.vertexOffset = meshlet.vertexOffset;
			desc.vertexCount = meshlet.vertexCount;
			desc.primitiveOffset = meshlet.primitiveOffset;
			desc.primitiveCount = meshlet.primitiveCount;
			desc.subMeshIndex = meshlet.subMeshIndex;
			result.emplace_back(desc);
		}
		return result;
	}

	std::vector<Engine::MeshletBounds> BuildMeshletBounds(const std::vector<Engine::MeshletDesc>& meshlets) {

		// ASのメッシュレット単位カリングで使うBoundsだけを分離する
		std::vector<Engine::MeshletBounds> result{};
		result.reserve(meshlets.size());
		for (const Engine::MeshletDesc& meshlet : meshlets) {

			result.emplace_back(Engine::MeshletBounds{ meshlet.boundsCenter, meshlet.boundsRadius,
				meshlet.coneAxis, meshlet.coneCutoff });
		}
		return result;
	}
}

uint32_t Engine::MeshUploadPreparation::PackDirection(const Vector3& direction) {

	return EncodeOctNormal(direction);
}

Engine::MeshUploadData Engine::MeshUploadPreparation::Build(const ImportedMeshAsset& imported) {

	// worker上で描画用配列と境界球を確定する
	MeshUploadData data{};
	data.hasVertexColors = !imported.vertexColors.empty();
	if (data.hasVertexColors && imported.vertexColors.size() != imported.vertices.size()) {
		throw std::runtime_error("Meshの頂点色と頂点数が一致しません");
	}
	data.vertexColors = data.hasVertexColors ? imported.vertexColors : std::vector<Vector4>{Vector4(1, 1, 1, 1)};
	MeshBoundsUtility::Calculate(imported.vertices, {}, data.boundsCenter, data.boundsRadius);
	// 重いIndex走査は読込worker上で一度だけ行う
	data.subMeshBounds.reserve(imported.subMeshes.size());
	for (const auto& subMesh : imported.subMeshes) {
		Vector3 center{};
		float radius = 0.0f;
		const auto indices = std::span<const uint32_t>(imported.indices).subspan(subMesh.indexOffset, subMesh.indexCount);
		if (!indices.empty()) MeshBoundsUtility::Calculate(imported.vertices, indices, center, radius);
		data.subMeshBounds.emplace_back(center.x, center.y, center.z, radius);
	}
	data.packedVertices = BuildPackedVertices(imported.vertices);
	data.useIndex16 = CanPackMeshletVertexIndices(imported.indices);
	if (data.useIndex16) { data.indices16 = BuildIndex16(imported.indices); }
	data.primitiveSubMeshIndices = BuildPrimitiveSubMeshTable(imported.subMeshes, imported.lods[0].indexCount);
	data.meshletDraws = BuildMeshletDrawDescs(imported.meshlets);
	data.meshletBounds = BuildMeshletBounds(imported.meshlets);
	data.usePackedMeshletIndices = CanPackMeshletVertexIndices(imported.meshletVertexIndices);
	if (data.usePackedMeshletIndices) {
		data.packedMeshletIndices = BuildPackedMeshletVertexIndices(imported.meshletVertexIndices);
	}
	return data;
}
