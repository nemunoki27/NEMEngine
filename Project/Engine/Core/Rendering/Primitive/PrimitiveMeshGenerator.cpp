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

	void HashFloat(uint64_t& hash, float value) {

		// 符号付き0で同じ形状を二重生成しない
		if (value == 0.0f) value = 0.0f;
		HashScalar(hash, &value, sizeof(value));
	}
	void HashInt(uint64_t& hash, int32_t value) { HashScalar(hash, &value, sizeof(value)); }
	void HashVector2(uint64_t& hash, const Engine::Vector2& value) { HashFloat(hash, value.x); HashFloat(hash, value.y); }
	void HashVector3(uint64_t& hash, const Engine::Vector3& value) { HashFloat(hash, value.x); HashFloat(hash, value.y); HashFloat(hash, value.z); }

	float FiniteOr(float value, float fallback) {

		return std::isfinite(value) ? value : fallback;
	}

	Engine::Vector2 FiniteOr(const Engine::Vector2& value, const Engine::Vector2& fallback) {

		return Engine::Vector2(
			FiniteOr(value.x, fallback.x),
			FiniteOr(value.y, fallback.y));
	}

	Engine::Vector3 FiniteOr(const Engine::Vector3& value, const Engine::Vector3& fallback) {

		return Engine::Vector3(
			FiniteOr(value.x, fallback.x),
			FiniteOr(value.y, fallback.y),
			FiniteOr(value.z, fallback.z));
	}

	Engine::PrimitiveRendererComponent NormalizeFiniteParameters(
		const Engine::PrimitiveRendererComponent& renderer) {

		Engine::PrimitiveRendererComponent result = renderer;
		switch (renderer.type) {
		case Engine::PrimitiveType::Plane:
			result.plane.size = FiniteOr(renderer.plane.size,
				Engine::Vector2::AnyInit(1.0f));
			result.plane.pivot = FiniteOr(renderer.plane.pivot,
				Engine::Vector2::AnyInit(0.5f));
			break;
		case Engine::PrimitiveType::CrossPlane:
			result.crossPlane.size = FiniteOr(renderer.crossPlane.size,
				Engine::Vector2::AnyInit(1.0f));
			result.crossPlane.pivot = FiniteOr(renderer.crossPlane.pivot,
				Engine::Vector2::AnyInit(0.5f));
			break;
		case Engine::PrimitiveType::Ring:
			result.ring.outerRadius = FiniteOr(renderer.ring.outerRadius, 1.0f);
			result.ring.innerRadius = FiniteOr(renderer.ring.innerRadius, 0.5f);
			result.ring.startAngle = FiniteOr(renderer.ring.startAngle, 0.0f);
			result.ring.endAngle = FiniteOr(renderer.ring.endAngle, 360.0f);
			break;
		case Engine::PrimitiveType::Cylinder:
			result.cylinder.topRadius = FiniteOr(renderer.cylinder.topRadius, 1.0f);
			result.cylinder.centerRadius = FiniteOr(renderer.cylinder.centerRadius, 1.0f);
			result.cylinder.bottomRadius = FiniteOr(renderer.cylinder.bottomRadius, 1.0f);
			result.cylinder.topRadiusWeight = FiniteOr(renderer.cylinder.topRadiusWeight, 0.0f);
			result.cylinder.bottomRadiusWeight = FiniteOr(renderer.cylinder.bottomRadiusWeight, 0.0f);
			result.cylinder.height = FiniteOr(renderer.cylinder.height, 2.0f);
			result.cylinder.maxAngle = FiniteOr(renderer.cylinder.maxAngle, 360.0f);
			break;
		case Engine::PrimitiveType::Sphere:
			result.sphere.radius = FiniteOr(renderer.sphere.radius, 1.0f);
			break;
		case Engine::PrimitiveType::Hemisphere:
			result.hemisphere.radius = FiniteOr(renderer.hemisphere.radius, 1.0f);
			break;
		case Engine::PrimitiveType::Cube:
			result.cube.size = FiniteOr(renderer.cube.size,
				Engine::Vector3::AnyInit(1.0f));
			result.cube.pivot = FiniteOr(renderer.cube.pivot,
				Engine::Vector3::AnyInit(0.5f));
			break;
		}
		return result;
	}

}

int32_t Engine::PrimitiveMeshGenerator::ClampDivide(int32_t value, int32_t minimum) {

	return std::clamp(value, minimum, kMaxPrimitiveDivide);
}

int32_t Engine::PrimitiveMeshGenerator::ClampCylinderHeightDivide(int32_t value) {

	// 中央半径の位置に必ず頂点列を置く
	const int32_t divide = ClampDivide(value, 2);
	return (divide & 1) != 0 && divide < kMaxPrimitiveDivide ? divide + 1 : divide;
}

void Engine::PrimitiveMeshGenerator::Generate(const PrimitiveRendererComponent& renderer, PrimitiveMeshData& out) {

	out.vertices.clear();
	out.indices.clear();
	const PrimitiveRendererComponent normalized = NormalizeFiniteParameters(renderer);

	switch (normalized.type) {
	case PrimitiveType::Plane:
		GeneratePlane(normalized.plane, out);
		break;
	case PrimitiveType::CrossPlane:
		GenerateCrossPlane(normalized.crossPlane, out);
		break;
	case PrimitiveType::Ring:
		GenerateRing(normalized.ring, out);
		break;
	case PrimitiveType::Cylinder:
		GenerateCylinder(normalized.cylinder, out);
		break;
	case PrimitiveType::Sphere:
		GenerateSphere(normalized.sphere, out);
		break;
	case PrimitiveType::Hemisphere:
		GenerateHemisphere(normalized.hemisphere, out);
		break;
	case PrimitiveType::Cube:
		GenerateCube(normalized.cube, out);
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
	std::vector<Vector3> bitangents(out.vertices.size(), Vector3::AnyInit(0.0f));

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
		const Vector3 bitangent = (edge2 * du1 - edge1 * du2) * (1.0f / denom);

		accum[i0] += tangent;
		accum[i1] += tangent;
		accum[i2] += tangent;
		bitangents[i0] += bitangent;
		bitangents[i1] += bitangent;
		bitangents[i2] += bitangent;
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
		// 従法線をUVのV方向へ合わせる
		out.vertices[i].tangentSign = Vector3::Dot(
			Vector3::Cross(normal, out.vertices[i].tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
	}
}

uint64_t Engine::PrimitiveMeshGenerator::ComputeHash(const PrimitiveRendererComponent& renderer) {

	uint64_t hash = kFnvOffsetBasis;
	HashInt(hash, static_cast<int32_t>(renderer.type));

	switch (renderer.type) {
	case PrimitiveType::Plane:
		HashVector2(hash, FiniteOr(renderer.plane.size, Vector2::AnyInit(1.0f)));
		HashVector2(hash, FiniteOr(renderer.plane.pivot, Vector2::AnyInit(0.5f)));
		HashInt(hash, static_cast<int32_t>(renderer.plane.axis));
		HashInt(hash, ClampDivide(renderer.plane.divideX, 1));
		HashInt(hash, ClampDivide(renderer.plane.divideY, 1));
		break;
	case PrimitiveType::CrossPlane:
		HashVector2(hash, FiniteOr(renderer.crossPlane.size, Vector2::AnyInit(1.0f)));
		HashVector2(hash, FiniteOr(renderer.crossPlane.pivot, Vector2::AnyInit(0.5f)));
		HashInt(hash, ClampDivide(renderer.crossPlane.planeCount, 1));
		break;
	case PrimitiveType::Ring:
		HashFloat(hash, FiniteOr(renderer.ring.outerRadius, 1.0f));
		HashFloat(hash, FiniteOr(renderer.ring.innerRadius, 0.5f));
		HashFloat(hash, FiniteOr(renderer.ring.startAngle, 0.0f));
		HashFloat(hash, FiniteOr(renderer.ring.endAngle, 360.0f));
		HashInt(hash, ClampDivide(renderer.ring.divide, 3));
		break;
	case PrimitiveType::Cylinder:
		HashFloat(hash, FiniteOr(renderer.cylinder.topRadius, 1.0f));
		HashFloat(hash, FiniteOr(renderer.cylinder.centerRadius, 1.0f));
		HashFloat(hash, FiniteOr(renderer.cylinder.bottomRadius, 1.0f));
		HashFloat(hash, std::clamp(FiniteOr(renderer.cylinder.topRadiusWeight, 0.0f), 0.0f, 1.0f));
		HashFloat(hash, std::clamp(FiniteOr(renderer.cylinder.bottomRadiusWeight, 0.0f), 0.0f, 1.0f));
		HashFloat(hash, FiniteOr(renderer.cylinder.height, 2.0f));
		HashFloat(hash, FiniteOr(renderer.cylinder.maxAngle, 360.0f));
		HashInt(hash, ClampDivide(renderer.cylinder.radialDivide, 3));
		HashInt(hash, ClampCylinderHeightDivide(renderer.cylinder.heightDivide));
		HashInt(hash, static_cast<int32_t>(renderer.cylinder.cap));
		HashInt(hash, static_cast<int32_t>(renderer.cylinder.uvMode));
		break;
	case PrimitiveType::Sphere:
		HashFloat(hash, FiniteOr(renderer.sphere.radius, 1.0f));
		HashInt(hash, ClampDivide(renderer.sphere.longitudeDivide, 3));
		HashInt(hash, ClampDivide(renderer.sphere.latitudeDivide, 2));
		break;
	case PrimitiveType::Hemisphere:
		HashFloat(hash, FiniteOr(renderer.hemisphere.radius, 1.0f));
		HashInt(hash, ClampDivide(renderer.hemisphere.longitudeDivide, 3));
		HashInt(hash, ClampDivide(renderer.hemisphere.latitudeDivide, 1));
		HashInt(hash, renderer.hemisphere.bottomCap ? 1 : 0);
		break;
	case PrimitiveType::Cube:
		HashVector3(hash, FiniteOr(renderer.cube.size, Vector3::AnyInit(1.0f)));
		HashVector3(hash, FiniteOr(renderer.cube.pivot, Vector3::AnyInit(0.5f)));
		break;
	}
	return hash;
}
