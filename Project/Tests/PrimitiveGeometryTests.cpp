#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Primitive/PrimitiveMeshGenerator.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>

// c++
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {

	bool TestNormalizedGeometryKeys() {

		using namespace Engine;
		const auto sameGeometry = [](const PrimitiveRendererComponent& first, const PrimitiveRendererComponent& second) {
			if (PrimitiveMeshGenerator::ComputeHash(first) != PrimitiveMeshGenerator::ComputeHash(second)) return false;
			PrimitiveMeshData a, b;
			PrimitiveMeshGenerator::Generate(first, a);
			PrimitiveMeshGenerator::Generate(second, b);
			if (a.vertices.size() != b.vertices.size() || a.indices != b.indices) return false;
			for (size_t index = 0; index < a.vertices.size(); ++index) {
				const auto& left = a.vertices[index];
				const auto& right = b.vertices[index];
				if (left.position != right.position || left.normal != right.normal || left.texcoord != right.texcoord ||
					left.tangent != right.tangent || left.tangentSign != right.tangentSign) return false;
			}
			return true;
		};
		// 最小分割数へ丸めた結果と元入力を同じ形状として共有する
		PrimitiveRendererComponent low, normalized;
		low.plane.divideX = -1;
		low.plane.divideY = 0;
		low.crossPlane.planeCount = 0;
		normalized.crossPlane.planeCount = 1;
		low.ring.divide = 0;
		normalized.ring.divide = 3;
		low.cylinder.radialDivide = 0;
		low.cylinder.heightDivide = 0;
		normalized.cylinder.radialDivide = 3;
		normalized.cylinder.heightDivide = 2;
		low.sphere.longitudeDivide = 0;
		low.sphere.latitudeDivide = 0;
		normalized.sphere.longitudeDivide = 3;
		normalized.sphere.latitudeDivide = 2;
		low.hemisphere.longitudeDivide = 0;
		low.hemisphere.latitudeDivide = 0;
		normalized.hemisphere.longitudeDivide = 3;
		normalized.hemisphere.latitudeDivide = 1;
		for (auto type : { PrimitiveType::Plane, PrimitiveType::CrossPlane, PrimitiveType::Ring,
			PrimitiveType::Cylinder, PrimitiveType::Sphere, PrimitiveType::Hemisphere, PrimitiveType::Cube }) {
			low.type = normalized.type = type;
			if (!sameGeometry(low, normalized)) return false;
		}
		// 円柱の偶数補正と重みの範囲補正もcache識別へ揃える
		low.type = normalized.type = PrimitiveType::Cylinder;
		low.cylinder.heightDivide = 3;
		normalized.cylinder.heightDivide = 4;
		low.cylinder.topRadiusWeight = 2.0f;
		normalized.cylinder.topRadiusWeight = 1.0f;
		low.cylinder.bottomRadiusWeight = -1.0f;
		low.cylinder.centerRadius = normalized.cylinder.centerRadius = 0.5f;
		if (!sameGeometry(low, normalized)) return false;
		low.type = normalized.type = PrimitiveType::Plane;
		low.plane.divideX = kMaxPrimitiveDivide + 7;
		normalized.plane.divideX = kMaxPrimitiveDivide;
		low.plane.pivot.x = -0.0f;
		normalized.plane.pivot.x = 0.0f;
		return sameGeometry(low, normalized);
	}

	bool TestFiniteParameters() {

		using namespace Engine;
		const float nan = std::numeric_limits<float>::quiet_NaN();
		const float infinity = std::numeric_limits<float>::infinity();
		const auto check = [](PrimitiveRendererComponent invalid,
			PrimitiveRendererComponent expected) {

			PrimitiveMeshData mesh;
			PrimitiveMeshGenerator::Generate(invalid, mesh);
			if (PrimitiveMeshGenerator::ComputeHash(invalid) !=
				PrimitiveMeshGenerator::ComputeHash(expected)) {
				return false;
			}
			for (const PrimitiveMeshVertex& vertex : mesh.vertices) {
				if (!std::isfinite(vertex.position.x) || !std::isfinite(vertex.position.y) ||
					!std::isfinite(vertex.position.z) || !std::isfinite(vertex.normal.x) ||
					!std::isfinite(vertex.normal.y) || !std::isfinite(vertex.normal.z) ||
					!std::isfinite(vertex.texcoord.x) || !std::isfinite(vertex.texcoord.y) ||
					!std::isfinite(vertex.tangent.x) || !std::isfinite(vertex.tangent.y) ||
					!std::isfinite(vertex.tangent.z)) {
					return false;
				}
			}
			return true;
		};

		PrimitiveRendererComponent expected;
		PrimitiveRendererComponent invalid = expected;
		invalid.type = expected.type = PrimitiveType::Plane;
		invalid.plane.size.x = nan;
		invalid.plane.pivot.y = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Ring;
		invalid.ring.outerRadius = nan;
		invalid.ring.endAngle = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Cylinder;
		invalid.cylinder.height = nan;
		invalid.cylinder.maxAngle = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Sphere;
		invalid.sphere.radius = nan;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Hemisphere;
		invalid.hemisphere.radius = infinity;
		if (!check(invalid, expected)) return false;

		invalid = expected;
		invalid.type = expected.type = PrimitiveType::Cube;
		invalid.cube.size.z = nan;
		invalid.cube.pivot.x = infinity;
		return check(invalid, expected);
	}

	bool TestGeometryKeyCollision() {

		using namespace Engine;
		struct ZeroHash {

			size_t operator()(const PrimitiveGeometryKey&) const { return 0; }
		};

		PrimitiveRendererComponent plane;
		PrimitiveRendererComponent sphere;
		sphere.type = PrimitiveType::Sphere;
		const PrimitiveGeometryKey planeKey = PrimitiveMeshGenerator::ComputeKey(plane);
		const PrimitiveGeometryKey sphereKey = PrimitiveMeshGenerator::ComputeKey(sphere);
		if (planeKey == sphereKey) return false;

		// 同じhash bucketでも形状キーの比較で別要素に分ける
		std::unordered_map<PrimitiveGeometryKey, int, ZeroHash> cache;
		cache.emplace(planeKey, 1);
		cache.emplace(sphereKey, 2);
		return cache.size() == 2 && cache.at(planeKey) == 1 && cache.at(sphereKey) == 2;
	}

	bool ValidateGeometryCapacity(const Engine::PrimitiveMeshData& mesh) {

		if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3 != 0 ||
			mesh.vertices.size() > std::numeric_limits<uint32_t>::max() ||
			mesh.indices.size() > std::numeric_limits<uint32_t>::max()) {
			return false;
		}
		for (uint32_t index : mesh.indices) {
			if (index >= mesh.vertices.size()) return false;
		}
		return true;
	}

	bool TestGeometryCapacity() {

		using namespace Engine;
		PrimitiveRendererComponent renderer;
		PrimitiveMeshData mesh;
		for (PrimitiveType type : { PrimitiveType::Plane, PrimitiveType::CrossPlane,
			PrimitiveType::Ring, PrimitiveType::Cylinder, PrimitiveType::Sphere,
			PrimitiveType::Hemisphere, PrimitiveType::Cube }) {

			renderer.type = type;
			renderer.plane.divideX = renderer.plane.divideY = kMaxPrimitiveDivide;
			renderer.crossPlane.planeCount = kMaxPrimitiveDivide;
			renderer.ring.divide = kMaxPrimitiveDivide;
			renderer.cylinder.radialDivide = renderer.cylinder.heightDivide = kMaxPrimitiveDivide;
			renderer.sphere.longitudeDivide = renderer.sphere.latitudeDivide = kMaxPrimitiveDivide;
			renderer.hemisphere.longitudeDivide = renderer.hemisphere.latitudeDivide = kMaxPrimitiveDivide;
			PrimitiveMeshGenerator::Generate(renderer, mesh);
			if (!ValidateGeometryCapacity(mesh)) return false;
		}
		return true;
	}

	bool TestTriangleWinding() {

		using namespace Engine;
		PrimitiveRendererComponent renderer;
		PrimitiveMeshData mesh;
		for (PrimitiveType type : { PrimitiveType::Plane, PrimitiveType::CrossPlane,
			PrimitiveType::Ring, PrimitiveType::Cylinder, PrimitiveType::Sphere,
			PrimitiveType::Hemisphere, PrimitiveType::Cube }) {

			renderer.type = type;
			PrimitiveMeshGenerator::Generate(renderer, mesh);
			for (size_t index = 0; index < mesh.indices.size(); index += 3) {

				const PrimitiveMeshVertex& a = mesh.vertices[mesh.indices[index + 0]];
				const PrimitiveMeshVertex& b = mesh.vertices[mesh.indices[index + 1]];
				const PrimitiveMeshVertex& c = mesh.vertices[mesh.indices[index + 2]];
				const Vector3 face = Vector3::Cross(b.position - a.position, c.position - a.position);
				if (face.Length() <= 1e-6f) continue;
				const Vector3 normal = a.normal + b.normal + c.normal;
				// D3Dの時計回り面と頂点法線の向きを揃える
				if (Vector3::Dot(face, normal) >= 0.0f) return false;
			}
		}
		return true;
	}
}

bool NEMTests::TestPrimitiveTangents() {

	using namespace Engine;
	PrimitiveRendererComponent renderer;
	PrimitiveMeshData mesh;
	// 平面の軸とU反転に合わせて接線・従法線が向くことを確認する
	for (auto axis : { PrimitivePlaneAxis::XY, PrimitivePlaneAxis::XZ, PrimitivePlaneAxis::YZ }) {
		for (float sizeX : { 2.0f, -2.0f }) {
			renderer.plane.axis = axis;
			renderer.plane.size = Vector2(sizeX, 3.0f);
			renderer.plane.divideX = 2;
			renderer.plane.divideY = 3;
			PrimitiveMeshGenerator::Generate(renderer, mesh);
			if (mesh.vertices.empty()) return false;
			const Vector3 expectedTangent = axis == PrimitivePlaneAxis::YZ ?
				Vector3(0.0f, sizeX < 0.0f ? -1.0f : 1.0f, 0.0f) :
				Vector3(sizeX < 0.0f ? -1.0f : 1.0f, 0.0f, 0.0f);
			const Vector3 expectedBitangent = axis == PrimitivePlaneAxis::XY ?
				Vector3(0.0f, -1.0f, 0.0f) : Vector3(0.0f, 0.0f, -1.0f);
			for (const auto& vertex : mesh.vertices) {
				const Vector3 bitangent = Vector3::Cross(vertex.normal, vertex.tangent) * vertex.tangentSign;
				if (Vector3::Dot(vertex.tangent, expectedTangent) < 0.999f ||
					Vector3::Dot(bitangent, expectedBitangent) < 0.999f) return false;
			}
		}
	}
	// 大きさが0の形状でも有限な直交基底を返す
	renderer.plane.size = Vector2::AnyInit(0.0f);
	PrimitiveMeshGenerator::Generate(renderer, mesh);
	for (const auto& vertex : mesh.vertices) {
		if (!std::isfinite(vertex.tangent.x) || !std::isfinite(vertex.tangent.y) ||
			!std::isfinite(vertex.tangent.z) || std::fabs(Vector3::Dot(vertex.normal, vertex.tangent)) > 0.001f ||
			std::fabs(vertex.tangent.Length() - 1.0f) > 0.001f) return false;
	}
	return TestNormalizedGeometryKeys() && TestFiniteParameters() && TestGeometryKeyCollision() &&
		TestGeometryCapacity() && TestTriangleWinding();
}
