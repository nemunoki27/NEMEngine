#include "FillFaceMeshRendererSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/FillFaceMeshRendererComponent.h>

// c++
#include <vector>
#include <cstdint>
#include <cstddef>

//============================================================================
//	FillFaceMeshRendererSystem classMethods
//============================================================================
namespace {

	// XZ平面で三角形abcの内側に点pがあるか
	bool PointInTriangleXZ(const Engine::Vector3& p, const Engine::Vector3& a,
		const Engine::Vector3& b, const Engine::Vector3& c) {

		const float d1 = (p.x - b.x) * (a.z - b.z) - (a.x - b.x) * (p.z - b.z);
		const float d2 = (p.x - c.x) * (b.z - c.z) - (b.x - c.x) * (p.z - c.z);
		const float d3 = (p.x - a.x) * (c.z - a.z) - (c.x - a.x) * (p.z - a.z);

		const bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
		const bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
		return !(hasNeg && hasPos);
	}

	// XZ平面の単純ポリゴンを耳切り法で三角形分割する
	std::vector<uint32_t> TriangulatePolygonXZ(
		std::span<const Engine::FillMeshPosition> points) {

		std::vector<uint32_t> indices;
		const size_t count = points.size();
		if (count < 3) {
			return indices;
		}

		// 符号付き面積でワインディングを判定する
		float area = 0.0f;
		for (size_t i = 0; i < count; ++i) {
			const Engine::Vector3& a = points[i].value;
			const Engine::Vector3& b = points[(i + 1) % count].value;
			area += a.x * b.z - b.x * a.z;
		}
		const bool clockwise = area < 0.0f;

		// 残り頂点のインデックスをCCW順で持つ
		std::vector<uint32_t> remaining(count);
		for (size_t i = 0; i < count; ++i) {
			remaining[i] = static_cast<uint32_t>(clockwise ? (count - 1 - i) : i);
		}

		// 耳を順に切り取る、無限ループ防止に上限を設ける
		size_t guard = count * count;
		while (remaining.size() > 3 && guard-- > 0) {

			bool earFound = false;
			const size_t n = remaining.size();
			for (size_t i = 0; i < n; ++i) {

				const uint32_t i0 = remaining[(i + n - 1) % n];
				const uint32_t i1 = remaining[i];
				const uint32_t i2 = remaining[(i + 1) % n];
				const Engine::Vector3& a = points[i0].value;
				const Engine::Vector3& b = points[i1].value;
				const Engine::Vector3& c = points[i2].value;

				// CCWで凸頂点なら外積が正
				const float cross = (b.x - a.x) * (c.z - a.z) - (b.z - a.z) * (c.x - a.x);
				if (cross <= 0.0f) {
					continue;
				}

				// 三角形内に他の頂点があれば耳ではない
				bool contains = false;
				for (size_t j = 0; j < n; ++j) {
					const uint32_t pj = remaining[j];
					if (pj == i0 || pj == i1 || pj == i2) {
						continue;
					}
					if (PointInTriangleXZ(points[pj].value, a, b, c)) {
						contains = true;
						break;
					}
				}
				if (contains) {
					continue;
				}

				// 耳を確定する
				indices.push_back(i0);
				indices.push_back(i1);
				indices.push_back(i2);
				remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(i));
				earFound = true;
				break;
			}

			// 耳が無い場合は不正ポリゴンなので打ち切る
			if (!earFound) {
				break;
			}
		}

		// 残った三角形を出力する
		if (remaining.size() == 3) {
			indices.push_back(remaining[0]);
			indices.push_back(remaining[1]);
			indices.push_back(remaining[2]);
		}
		return indices;
	}
}

void Engine::FillFaceMeshRendererSystem::Update(ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	world.ForEach<FillMeshRendererComponent>([&](Entity entity, FillMeshRendererComponent& fillMesh) {

		// buildMeshが立っているフレームだけ構築する
		if (!fillMesh.buildMesh) {
			return;
		}

		// 点列から三角形分割し、Runtime用Bufferと更新世代を差し替える
		const std::vector<uint32_t> indices =
			TriangulatePolygonXZ(GetFillMeshPositions(world, entity));
		SetFillMeshTriangleIndices(world, entity, indices);
		if (FillMeshRuntimeStateComponent* state =
			world.TryGetComponent<FillMeshRuntimeStateComponent>(entity)) {
			++state->geometryGeneration;
		}
		fillMesh.buildMesh = false;
		});
}
