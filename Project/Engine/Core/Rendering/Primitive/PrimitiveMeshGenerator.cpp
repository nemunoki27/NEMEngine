#include "PrimitiveMeshGenerator.h"

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>
#include <numbers>

//============================================================================
//	PrimitiveMeshGenerator classMethods
//============================================================================
namespace {

	// FNV-1aハッシュの基底と素数
	constexpr uint64_t kFnvOffsetBasis = 1469598103934665603ull;
	constexpr uint64_t kFnvPrime = 1099511628211ull;

	// 1つの値をハッシュへ畳み込む、構造体のパディングを混ぜないよう成分単位で使う
	void HashScalar(uint64_t& hash, const void* data, size_t size) {

		const uint8_t* bytes = static_cast<const uint8_t*>(data);
		for (size_t i = 0; i < size; ++i) {
			hash ^= bytes[i];
			hash *= kFnvPrime;
		}
	}

	void HashFloat(uint64_t& hash, float value) { HashScalar(hash, &value, sizeof(value)); }
	void HashInt(uint64_t& hash, int32_t value) { HashScalar(hash, &value, sizeof(value)); }
	void HashVector2(uint64_t& hash, const Engine::Vector2& value) { HashFloat(hash, value.x); HashFloat(hash, value.y); }
	void HashVector3(uint64_t& hash, const Engine::Vector3& value) { HashFloat(hash, value.x); HashFloat(hash, value.y); HashFloat(hash, value.z); }

	// 直線補間

}

void Engine::PrimitiveMeshGenerator::Generate(const PrimitiveRendererComponent& renderer, PrimitiveMeshData& out) {

	out.vertices.clear();
	out.indices.clear();

	switch (renderer.type) {
	case PrimitiveType::Plane:
		GeneratePlane(renderer.plane, out);
		break;
	case PrimitiveType::CrossPlane:
		GenerateCrossPlane(renderer.crossPlane, out);
		break;
	case PrimitiveType::Ring:
		GenerateRing(renderer.ring, out);
		break;
	case PrimitiveType::Cylinder:
		GenerateCylinder(renderer.cylinder, out);
		break;
	case PrimitiveType::Sphere:
		GenerateSphere(renderer.sphere, out);
		break;
	case PrimitiveType::Hemisphere:
		GenerateHemisphere(renderer.hemisphere, out);
		break;
	case PrimitiveType::Cube:
		GenerateCube(renderer.cube, out);
		break;
	}

	// 法線マップのTBN構築に使う接線を位置とUVから求める
	ComputeTangents(out);
}

void Engine::PrimitiveMeshGenerator::ComputeTangents(PrimitiveMeshData& out) {

	if (out.vertices.empty() || out.indices.size() < 3) {
		return;
	}

	std::vector<Vector3> accum(out.vertices.size(), Vector3::AnyInit(0.0f));

	// 三角形ごとに位置差とUV差から接線を求めて頂点へ加算する
	for (size_t i = 0; i + 2 < out.indices.size(); i += 3) {

		const uint32_t i0 = out.indices[i + 0];
		const uint32_t i1 = out.indices[i + 1];
		const uint32_t i2 = out.indices[i + 2];

		const Vector3 edge1 = out.vertices[i1].position - out.vertices[i0].position;
		const Vector3 edge2 = out.vertices[i2].position - out.vertices[i0].position;
		const float du1 = out.vertices[i1].texcoord.x - out.vertices[i0].texcoord.x;
		const float dv1 = out.vertices[i1].texcoord.y - out.vertices[i0].texcoord.y;
		const float du2 = out.vertices[i2].texcoord.x - out.vertices[i0].texcoord.x;
		const float dv2 = out.vertices[i2].texcoord.y - out.vertices[i0].texcoord.y;

		const float denom = du1 * dv2 - du2 * dv1;
		if (std::abs(denom) < 1e-8f) {
			continue;
		}
		const Vector3 tangent = (edge1 * dv2 - edge2 * dv1) * (1.0f / denom);

		accum[i0] += tangent;
		accum[i1] += tangent;
		accum[i2] += tangent;
	}

	// 法線に直交させて正規化する、UVが退化した頂点は法線から補助軸を作る
	for (size_t i = 0; i < out.vertices.size(); ++i) {

		const Vector3 normal = out.vertices[i].normal;
		Vector3 tangent = accum[i] - normal * Vector3::Dot(normal, accum[i]);
		if (Vector3::Length(tangent) < 1e-6f) {

			const Vector3 axis = std::abs(normal.y) < 0.99f ? Vector3(0.0f, 1.0f, 0.0f) : Vector3(1.0f, 0.0f, 0.0f);
			tangent = Vector3::Cross(axis, normal);
		}
		out.vertices[i].tangent = Vector3::Normalize(tangent);
	}
}

uint64_t Engine::PrimitiveMeshGenerator::ComputeHash(const PrimitiveRendererComponent& renderer) {

	uint64_t hash = kFnvOffsetBasis;
	HashInt(hash, static_cast<int32_t>(renderer.type));

	switch (renderer.type) {
	case PrimitiveType::Plane:
		HashVector2(hash, renderer.plane.size);
		HashVector2(hash, renderer.plane.pivot);
		HashInt(hash, static_cast<int32_t>(renderer.plane.axis));
		HashInt(hash, renderer.plane.divideX);
		HashInt(hash, renderer.plane.divideY);
		break;
	case PrimitiveType::CrossPlane:
		HashVector2(hash, renderer.crossPlane.size);
		HashVector2(hash, renderer.crossPlane.pivot);
		HashInt(hash, renderer.crossPlane.planeCount);
		break;
	case PrimitiveType::Ring:
		HashFloat(hash, renderer.ring.outerRadius);
		HashFloat(hash, renderer.ring.innerRadius);
		HashFloat(hash, renderer.ring.startAngle);
		HashFloat(hash, renderer.ring.endAngle);
		HashInt(hash, renderer.ring.divide);
		break;
	case PrimitiveType::Cylinder:
		HashFloat(hash, renderer.cylinder.topRadius);
		HashFloat(hash, renderer.cylinder.centerRadius);
		HashFloat(hash, renderer.cylinder.bottomRadius);
		HashFloat(hash, renderer.cylinder.topRadiusWeight);
		HashFloat(hash, renderer.cylinder.bottomRadiusWeight);
		HashFloat(hash, renderer.cylinder.height);
		HashFloat(hash, renderer.cylinder.maxAngle);
		HashInt(hash, renderer.cylinder.radialDivide);
		HashInt(hash, renderer.cylinder.heightDivide);
		HashInt(hash, static_cast<int32_t>(renderer.cylinder.cap));
		HashInt(hash, static_cast<int32_t>(renderer.cylinder.uvMode));
		break;
	case PrimitiveType::Sphere:
		HashFloat(hash, renderer.sphere.radius);
		HashInt(hash, renderer.sphere.longitudeDivide);
		HashInt(hash, renderer.sphere.latitudeDivide);
		break;
	case PrimitiveType::Hemisphere:
		HashFloat(hash, renderer.hemisphere.radius);
		HashInt(hash, renderer.hemisphere.longitudeDivide);
		HashInt(hash, renderer.hemisphere.latitudeDivide);
		HashInt(hash, renderer.hemisphere.bottomCap ? 1 : 0);
		break;
	case PrimitiveType::Cube:
		HashVector3(hash, renderer.cube.size);
		HashVector3(hash, renderer.cube.pivot);
		break;
	}
	return hash;
}
