#include "BakedLightingGeometry.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/Import/MeshFileImporter.h>
#include <Engine/Core/Rendering/Meshes/Utility/MeshNormalMatrixUtility.h>
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshUploadPreparation.h>

// c++
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <numeric>
#include <stdexcept>
// xatlas
#include <xatlas.h>

namespace {

	using namespace Engine;
	struct AtlasOwner {

		xatlas::Atlas* atlas = xatlas::Create();
		~AtlasOwner() { xatlas::Destroy(atlas); }
	};
	struct AtlasProgress {

		const std::atomic<bool>& cancelled;
		std::atomic<float>& progress;
	};
	// xatlasの作業中も取消を確認する
	bool ReportAtlasProgress(xatlas::ProgressCategory category, int percent, void* user) {

		auto& status = *static_cast<AtlasProgress*>(user);
		status.progress.store(0.15f + 0.2f * float(static_cast<uint32_t>(category)) + 0.0015f * percent);
		return !status.cancelled.load();
	}
	// UVから三角形内の補間係数を求める
	Vector3 Barycentric(Vector2 point, Vector2 a, Vector2 b, Vector2 c) {

		const Vector2 ab = b - a;
		const Vector2 ac = c - a;
		const Vector2 ap = point - a;
		const float determinant = ab.x * ac.y - ab.y * ac.x;
		if (std::abs(determinant) < 1e-8f) return Vector3(-1.0f, -1.0f, -1.0f);
		const float second = (ap.x * ac.y - ap.y * ac.x) / determinant;
		const float third = (ab.x * ap.y - ab.y * ap.x) / determinant;
		return Vector3(1.0f - second - third, second, third);
	}
	// 三角形の内部だけ受光位置を配置する
	void Rasterize(const BakedLightingTriangle& triangle, uint32_t size, std::vector<BakedLightingTexel>& texels) {

		std::array<Vector2, 3> uv{};
		for (uint32_t index = 0; index < 3; ++index) {
			uv[index] = Vector2(triangle.vertices[index].lightmapU, triangle.vertices[index].lightmapV) * float(size);
		}
		const int minimumX = std::max(0, int(std::floor(std::min({uv[0].x, uv[1].x, uv[2].x}))));
		const int minimumY = std::max(0, int(std::floor(std::min({uv[0].y, uv[1].y, uv[2].y}))));
		const int maximumX = std::min(int(size) - 1, int(std::ceil(std::max({uv[0].x, uv[1].x, uv[2].x}))));
		const int maximumY = std::min(int(size) - 1, int(std::ceil(std::max({uv[0].y, uv[1].y, uv[2].y}))));
		for (int y = minimumY; y <= maximumY; ++y) {
			for (int x = minimumX; x <= maximumX; ++x) {
				const Vector3 weights = Barycentric(Vector2(float(x) + 0.5f, float(y) + 0.5f), uv[0], uv[1], uv[2]);
				if (std::min({weights.x, weights.y, weights.z}) < -1e-5f) continue;
				auto& texel = texels[(triangle.atlasIndex * size + uint32_t(y)) * size + uint32_t(x)];
				texel.position = triangle.vertices[0].position * weights.x + triangle.vertices[1].position * weights.y +
					triangle.vertices[2].position * weights.z;
				texel.normal = Vector3::Normalize(triangle.vertices[0].normal * weights.x +
					triangle.vertices[1].normal * weights.y + triangle.vertices[2].normal * weights.z);
				texel.valid = 1;
				texel.chart = triangle.chartIndex;
			}
		}
	}
}

void Engine::BakedLightingGeometry::Prepare(BakedLightingPreparation& output, const BakedLightingSettings& settings,
	const std::atomic<bool>& cancelled, std::atomic<float>& progress) {

	AtlasOwner owner;
	AtlasProgress status{cancelled, progress};
	xatlas::SetProgressCallback(owner.atlas, &ReportAtlasProgress, &status);
	std::vector<std::vector<BakedLightingVertex>> meshes;
	std::vector<std::vector<uint32_t>> meshIndices;
	std::vector<std::vector<uint32_t>> meshMaterials;
	double surfaceArea = 0.0;
	for (const auto& input : output.meshes) {
		if (cancelled.load()) return;
		// 描画経路と同じ取込処理からLOD0を取得する
		auto imported = MeshFileImporter::ImportFile(input.mesh, input.path, input.importSettings, {}, false);
		if (imported.isSkinned) throw std::runtime_error("Skinned Meshは固定背景としてベイクできません");
		std::vector<BakedLightingVertex> vertices;
		std::vector<uint32_t> indices;
		std::vector<uint32_t> materials;
		for (uint32_t subIndex = 0; subIndex < imported.subMeshes.size(); ++subIndex) {
			const auto& subMesh = imported.subMeshes[subIndex];
			if (subIndex >= input.subMeshes.size() || !input.subMeshes[subIndex].visible) continue;
			const auto& authoring = input.subMeshes[subIndex];
			const auto worldMatrix = MeshSubMeshRuntime::BuildRenderLocalMatrix(authoring) * input.worldMatrix;
			const auto normalMatrix = BuildSafeMeshNormalMatrix(worldMatrix);
			if (normalMatrix.usedFallback) throw std::runtime_error("固定背景のTransformが退化しています");
			for (uint32_t index = 0; index < subMesh.indexCount; ++index) {
				const auto& vertex = imported.vertices.at(imported.indices.at(subMesh.indexOffset + index));
				BakedLightingVertex outputVertex{};
				outputVertex.position = Vector3::Transform(Vector3(vertex.position.x, vertex.position.y, vertex.position.z), worldMatrix);
				const Vector3 transformedNormal = Vector3::TransferNormal(vertex.normal, normalMatrix.matrix);
				outputVertex.normal = Vector3::Normalize(transformedNormal);
				const Vector3 tangent = Vector3::TransferNormal(vertex.tangent, worldMatrix);
				outputVertex.tangentOct = MeshUploadPreparation::PackDirection(Vector3::Normalize(tangent));
				outputVertex.tangentSign = vertex.tangentSign * normalMatrix.orientationSign;
				outputVertex.uv = vertex.uv;
				const auto uvMatrix = output.materials.at(input.materials.at(subIndex)).uvMatrix;
				const Vector3 transformedUV = Vector3::Transform(Vector3(vertex.uv.x, vertex.uv.y, 0.0f), uvMatrix);
				outputVertex.uv = Vector2(transformedUV.x, transformedUV.y);
				if (!std::isfinite(outputVertex.position.x) || !std::isfinite(outputVertex.position.y) ||
					!std::isfinite(outputVertex.position.z) || !std::isfinite(outputVertex.normal.x) ||
					!std::isfinite(outputVertex.normal.y) || !std::isfinite(outputVertex.normal.z)) {
					throw std::runtime_error("固定背景の頂点または法線が不正です");
				}
				indices.push_back(static_cast<uint32_t>(vertices.size()));
				vertices.push_back(outputVertex);
				if (vertices.size() > 3u * 1024u * 1024u) throw std::runtime_error("固定背景の面数が上限を超えています ベイク対象を分けてください");
				if (index % 3u == 0u) materials.push_back(input.materials.at(subIndex));
			}
		}
		if (indices.empty()) throw std::runtime_error("ベイク対象MeshのLOD0が空です");
		for (size_t index = 0; index + 2 < vertices.size(); index += 3) {
			const Vector3 first = vertices[index + 1].position - vertices[index].position;
			const Vector3 second = vertices[index + 2].position - vertices[index].position;
			surfaceArea += Vector3::Length(Vector3::Cross(first, second)) * 0.5;
		}
		xatlas::MeshDecl declaration{};
		declaration.vertexPositionData = &vertices.front().position;
		declaration.vertexPositionStride = sizeof(BakedLightingVertex);
		declaration.vertexNormalData = &vertices.front().normal;
		declaration.vertexNormalStride = sizeof(BakedLightingVertex);
		declaration.vertexCount = static_cast<uint32_t>(vertices.size());
		declaration.indexData = indices.data();
		declaration.indexCount = static_cast<uint32_t>(indices.size());
		declaration.indexFormat = xatlas::IndexFormat::UInt32;
		declaration.faceMaterialData = materials.data();
		const auto result = xatlas::AddMesh(owner.atlas, declaration);
		if (result != xatlas::AddMeshError::Success) throw std::runtime_error(xatlas::StringForEnum(result));
		meshes.push_back(std::move(vertices));
		meshIndices.push_back(std::move(indices));
		meshMaterials.push_back(std::move(materials));
	}
	if (meshes.empty()) throw std::runtime_error("固定背景としてベイクするMeshがありません");
	xatlas::PackOptions packing{};
	packing.resolution = settings.atlasSize;
	// 密度を明示しAtlasを指定した正方形へ固定する
	if (!std::isfinite(surfaceArea) || surfaceArea <= 1e-8) throw std::runtime_error("固定背景の面積が不正です");
	packing.texelsPerUnit = static_cast<float>(double(settings.atlasSize) * 0.7 / std::sqrt(surfaceArea));
	packing.maxChartSize = settings.atlasSize - 12;
	packing.padding = 6;
	packing.bilinear = true;
	packing.blockAlign = true;
	packing.createImage = true;
	xatlas::Generate(owner.atlas, {}, packing);
	if (cancelled.load()) return;
	if (owner.atlas->atlasCount == 0 || owner.atlas->atlasCount > 32 || owner.atlas->width != owner.atlas->height) {
		throw std::runtime_error("Lightmap Atlasの数または寸法が上限を超えています");
	}
	auto& data = output.data;
	data.atlasSize = owner.atlas->width;
	data.atlasCount = owner.atlas->atlasCount;
	if (uint64_t(data.atlasSize) * data.atlasSize * data.atlasCount > 4u * 1024u * 1024u) {
		throw std::runtime_error("Lightmapの合計サイズが上限を超えています Atlasサイズを下げてください");
	}
	output.texels.resize(size_t(data.atlasSize) * data.atlasSize * data.atlasCount);
	for (uint32_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex) {
		const auto& atlasMesh = owner.atlas->meshes[meshIndex];
		for (uint32_t face = 0; face < atlasMesh.indexCount / 3; ++face) {
			BakedLightingTriangle triangle{};
			triangle.materialIndex = meshMaterials[meshIndex][face];
			triangle.receiverIndex = output.meshes[meshIndex].receiverIndex;
			for (uint32_t corner = 0; corner < 3; ++corner) {
				const auto& atlasVertex = atlasMesh.vertexArray[atlasMesh.indexArray[face * 3 + corner]];
				if (atlasVertex.atlasIndex < 0) throw std::runtime_error("ベイク用UVを生成できない面があります");
				triangle.vertices[corner] = meshes[meshIndex][atlasVertex.xref];
				triangle.vertices[corner].lightmapU = atlasVertex.uv[0] / float(data.atlasSize);
				triangle.vertices[corner].lightmapV = atlasVertex.uv[1] / float(data.atlasSize);
				triangle.atlasIndex = static_cast<uint32_t>(atlasVertex.atlasIndex);
				triangle.chartIndex = (meshIndex << 20u) | static_cast<uint32_t>(atlasVertex.chartIndex);
			}
			Rasterize(triangle, data.atlasSize, output.texels);
			data.triangles.push_back(triangle);
		}
	}
	BuildHierarchy(data);
	progress.store(1.0f);
}

void Engine::BakedLightingGeometry::BuildHierarchy(BakedLightingData& data) {

	data.triangleOrder.resize(data.triangles.size());
	std::iota(data.triangleOrder.begin(), data.triangleOrder.end(), 0u);
	data.nodes.clear();
	// 葉の三角形範囲と子Nodeの番号を同じ階層へ保持する
	auto build = [&](auto&& self, uint32_t begin, uint32_t end) -> uint32_t {
		const uint32_t nodeIndex = static_cast<uint32_t>(data.nodes.size());
		data.nodes.emplace_back();
		Vector3 minimum = Vector3::AnyInit(FLT_MAX);
		Vector3 maximum = Vector3::AnyInit(-FLT_MAX);
		for (uint32_t index = begin; index < end; ++index) {
			for (const auto& vertex : data.triangles[data.triangleOrder[index]].vertices) {
				minimum = Vector3(std::min(minimum.x, vertex.position.x), std::min(minimum.y, vertex.position.y), std::min(minimum.z, vertex.position.z));
				maximum = Vector3(std::max(maximum.x, vertex.position.x), std::max(maximum.y, vertex.position.y), std::max(maximum.z, vertex.position.z));
			}
		}
		if (end - begin <= 4) {
			data.nodes[nodeIndex] = {minimum, begin | 0x80000000u, maximum, end - begin};
			return nodeIndex;
		}
		const Vector3 extent = maximum - minimum;
		const uint32_t axis = extent.x >= extent.y && extent.x >= extent.z ? 0 : extent.y >= extent.z ? 1 : 2;
		const uint32_t middle = (begin + end) / 2;
		auto center = [&](uint32_t index) {
			const auto& vertices = data.triangles[index].vertices;
			const Vector3 value = vertices[0].position + vertices[1].position + vertices[2].position;
			return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
		};
		std::nth_element(data.triangleOrder.begin() + begin, data.triangleOrder.begin() + middle,
			data.triangleOrder.begin() + end, [&](uint32_t a, uint32_t b) { return center(a) < center(b); });
		const uint32_t left = self(self, begin, middle);
		const uint32_t right = self(self, middle, end);
		data.nodes[nodeIndex] = {minimum, left, maximum, right};
		return nodeIndex;
	};
	if (!data.triangles.empty()) build(build, 0, static_cast<uint32_t>(data.triangles.size()));
}
