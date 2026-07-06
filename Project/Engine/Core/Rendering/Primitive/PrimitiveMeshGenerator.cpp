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
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
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
		HashFloat(hash, renderer.cylinder.bottomRadius);
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

void Engine::PrimitiveMeshGenerator::GeneratePlane(const PrimitivePlaneParams& params, PrimitiveMeshData& out) {

	const int32_t divideX = std::clamp(params.divideX, 1, kMaxPrimitiveDivide);
	const int32_t divideY = std::clamp(params.divideY, 1, kMaxPrimitiveDivide);
	const float halfX = params.size.x * 0.5f;
	const float halfY = params.size.y * 0.5f;
	// pivotを原点に合わせるためのオフセット
	const float offsetX = Lerp(-halfX, halfX, params.pivot.x);
	const float offsetY = Lerp(-halfY, halfY, params.pivot.y);

	for (int32_t y = 0; y <= divideY; ++y) {
		for (int32_t x = 0; x <= divideX; ++x) {

			const float u = static_cast<float>(x) / static_cast<float>(divideX);
			const float v = static_cast<float>(y) / static_cast<float>(divideY);
			const float px = Lerp(-halfX, halfX, u) - offsetX;
			const float py = Lerp(-halfY, halfY, v) - offsetY;

			PrimitiveMeshVertex vertex{};
			vertex.texcoord = Vector2(u, 1.0f - v);
			switch (params.axis) {
			case PrimitivePlaneAxis::XZ:
				vertex.position = Vector3(px, 0.0f, py);
				vertex.normal = Vector3(0.0f, 1.0f, 0.0f);
				break;
			case PrimitivePlaneAxis::YZ:
				vertex.position = Vector3(0.0f, px, py);
				vertex.normal = Vector3(1.0f, 0.0f, 0.0f);
				break;
			case PrimitivePlaneAxis::XY:
			default:
				vertex.position = Vector3(px, py, 0.0f);
				vertex.normal = Vector3(0.0f, 0.0f, 1.0f);
				break;
			}
			out.vertices.push_back(vertex);
		}
	}

	const int32_t stride = divideX + 1;
	for (int32_t y = 0; y < divideY; ++y) {
		for (int32_t x = 0; x < divideX; ++x) {

			const uint32_t i0 = static_cast<uint32_t>(y * stride + x);
			const uint32_t i1 = i0 + 1;
			const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
			const uint32_t i3 = i2 + 1;
			out.indices.insert(out.indices.end(), { i0, i2, i1, i1, i2, i3 });
		}
	}
}

void Engine::PrimitiveMeshGenerator::GenerateSphere(const PrimitiveSphereParams& params, PrimitiveMeshData& out) {

	const int32_t longitude = std::clamp(params.longitudeDivide, 3, kMaxPrimitiveDivide);
	const int32_t latitude = std::clamp(params.latitudeDivide, 2, kMaxPrimitiveDivide);
	const float radius = params.radius;
	constexpr float pi = std::numbers::pi_v<float>;

	for (int32_t y = 0; y <= latitude; ++y) {

		const float v = static_cast<float>(y) / static_cast<float>(latitude);
		const float theta = pi * v;
		const float sinTheta = std::sin(theta);
		const float cosTheta = std::cos(theta);

		for (int32_t x = 0; x <= longitude; ++x) {

			const float u = static_cast<float>(x) / static_cast<float>(longitude);
			const float phi = pi * 2.0f * u;
			const float sinPhi = std::sin(phi);
			const float cosPhi = std::cos(phi);

			PrimitiveMeshVertex vertex{};
			vertex.normal = Vector3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
			vertex.position = Vector3(vertex.normal.x * radius, vertex.normal.y * radius, vertex.normal.z * radius);
			vertex.texcoord = Vector2(u, v);
			out.vertices.push_back(vertex);
		}
	}

	const int32_t stride = longitude + 1;
	for (int32_t y = 0; y < latitude; ++y) {
		for (int32_t x = 0; x < longitude; ++x) {

			const uint32_t i0 = static_cast<uint32_t>(y * stride + x);
			const uint32_t i1 = i0 + 1;
			const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
			const uint32_t i3 = i2 + 1;
			out.indices.insert(out.indices.end(), { i0, i2, i1, i1, i2, i3 });
		}
	}
}

void Engine::PrimitiveMeshGenerator::GenerateCrossPlane(const PrimitiveCrossPlaneParams& params, PrimitiveMeshData& out) {

	const int32_t planeCount = std::clamp(params.planeCount, 1, kMaxPrimitiveDivide);
	const float halfX = params.size.x * 0.5f;
	const float halfY = params.size.y * 0.5f;
	const float offsetX = Lerp(-halfX, halfX, params.pivot.x);
	const float offsetY = Lerp(-halfY, halfY, params.pivot.y);
	constexpr float pi = std::numbers::pi_v<float>;

	// 中心で交差する縦板をY軸まわりに均等配置する
	for (int32_t plane = 0; plane < planeCount; ++plane) {

		const float angle = pi * static_cast<float>(plane) / static_cast<float>(planeCount);
		const float sinA = std::sin(angle);
		const float cosA = std::cos(angle);
		const Vector3 normal(sinA, 0.0f, cosA);
		const uint32_t base = static_cast<uint32_t>(out.vertices.size());

		// 四隅、ローカルXYを回転してワールドへ置く
		const float cornerX[4] = { -halfX, halfX, -halfX, halfX };
		const float cornerY[4] = { -halfY, -halfY, halfY, halfY };
		const float cornerU[4] = { 0.0f, 1.0f, 0.0f, 1.0f };
		const float cornerV[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
		for (int32_t i = 0; i < 4; ++i) {

			const float lx = cornerX[i] - offsetX;
			const float ly = cornerY[i] - offsetY;
			PrimitiveMeshVertex vertex{};
			vertex.position = Vector3(lx * cosA, ly, -lx * sinA);
			vertex.normal = normal;
			vertex.texcoord = Vector2(cornerU[i], cornerV[i]);
			out.vertices.push_back(vertex);
		}
		out.indices.insert(out.indices.end(), { base, base + 2, base + 1, base + 1, base + 2, base + 3 });
	}
}

void Engine::PrimitiveMeshGenerator::GenerateRing(const PrimitiveRingParams& params, PrimitiveMeshData& out) {

	const int32_t divide = std::clamp(params.divide, 3, kMaxPrimitiveDivide);
	const float outer = params.outerRadius;
	const float inner = params.innerRadius;
	// 度数法の開始角から終了角までをラジアンへ直して分割する、全周なら従来どおり閉じたリングになる
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
	const float startAngle = params.startAngle * degToRad;
	const float angleStep = (params.endAngle - params.startAngle) * degToRad / static_cast<float>(divide);

	// XY平面のリング、各角度で外周と内周の2頂点を並べる
	for (int32_t i = 0; i <= divide; ++i) {

		const float angle = startAngle + angleStep * static_cast<float>(i);
		const float sinA = std::sin(angle);
		const float cosA = std::cos(angle);
		const float u = static_cast<float>(i) / static_cast<float>(divide);

		PrimitiveMeshVertex outerVertex{};
		outerVertex.position = Vector3(cosA * outer, sinA * outer, 0.0f);
		outerVertex.normal = Vector3(0.0f, 0.0f, 1.0f);
		outerVertex.texcoord = Vector2(u, 0.0f);
		out.vertices.push_back(outerVertex);

		PrimitiveMeshVertex innerVertex{};
		innerVertex.position = Vector3(cosA * inner, sinA * inner, 0.0f);
		innerVertex.normal = Vector3(0.0f, 0.0f, 1.0f);
		innerVertex.texcoord = Vector2(u, 1.0f);
		out.vertices.push_back(innerVertex);
	}

	for (int32_t i = 0; i < divide; ++i) {

		const uint32_t o0 = static_cast<uint32_t>(i * 2);
		const uint32_t in0 = o0 + 1;
		const uint32_t o1 = o0 + 2;
		const uint32_t in1 = o0 + 3;
		out.indices.insert(out.indices.end(), { o0, o1, in0, in0, o1, in1 });
	}
}

void Engine::PrimitiveMeshGenerator::GenerateCylinder(const PrimitiveCylinderParams& params, PrimitiveMeshData& out) {

	const int32_t radialDivide = std::clamp(params.radialDivide, 3, kMaxPrimitiveDivide);
	const int32_t heightDivide = std::clamp(params.heightDivide, 1, kMaxPrimitiveDivide);
	const float topRadius = params.topRadius;
	const float bottomRadius = params.bottomRadius;
	const float height = params.height;
	const float halfHeight = height * 0.5f;
	const float angleStep = params.maxAngle / static_cast<float>(radialDivide);
	const float slopeY = bottomRadius - topRadius;

	// 側面、高さと円周のグリッド、中心はローカル原点
	for (int32_t h = 0; h <= heightDivide; ++h) {

		const float th = static_cast<float>(h) / static_cast<float>(heightDivide);
		const float radius = Lerp(bottomRadius, topRadius, th);
		const float y = Lerp(-halfHeight, halfHeight, th);

		for (int32_t i = 0; i <= radialDivide; ++i) {

			const float angle = angleStep * static_cast<float>(i);
			const float sinA = std::sin(angle);
			const float cosA = std::cos(angle);

			PrimitiveMeshVertex vertex{};
			vertex.position = Vector3(cosA * radius, y, sinA * radius);
			vertex.normal = Vector3::Normalize(Vector3(cosA * height, slopeY, sinA * height));
			vertex.texcoord = Vector2(static_cast<float>(i) / static_cast<float>(radialDivide), 1.0f - th);
			out.vertices.push_back(vertex);
		}
	}

	const int32_t stride = radialDivide + 1;
	for (int32_t h = 0; h < heightDivide; ++h) {
		for (int32_t i = 0; i < radialDivide; ++i) {

			const uint32_t i0 = static_cast<uint32_t>(h * stride + i);
			const uint32_t i1 = i0 + 1;
			const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
			const uint32_t i3 = i2 + 1;
			out.indices.insert(out.indices.end(), { i0, i2, i1, i1, i2, i3 });
		}
	}

	// フタ、中心頂点とリングで扇状に張る
	const auto buildCap = [&](float y, float radius, const Vector3& normal, bool flip) {

		const uint32_t center = static_cast<uint32_t>(out.vertices.size());
		PrimitiveMeshVertex centerVertex{};
		centerVertex.position = Vector3(0.0f, y, 0.0f);
		centerVertex.normal = normal;
		centerVertex.texcoord = Vector2(0.5f, 0.5f);
		out.vertices.push_back(centerVertex);

		for (int32_t i = 0; i <= radialDivide; ++i) {

			const float angle = angleStep * static_cast<float>(i);
			const float sinA = std::sin(angle);
			const float cosA = std::cos(angle);
			PrimitiveMeshVertex vertex{};
			vertex.position = Vector3(cosA * radius, y, sinA * radius);
			vertex.normal = normal;
			vertex.texcoord = Vector2(cosA * 0.5f + 0.5f, sinA * 0.5f + 0.5f);
			out.vertices.push_back(vertex);
		}
		for (int32_t i = 0; i < radialDivide; ++i) {

			const uint32_t r0 = center + 1 + static_cast<uint32_t>(i);
			const uint32_t r1 = r0 + 1;
			if (flip) {
				out.indices.insert(out.indices.end(), { center, r1, r0 });
			} else {
				out.indices.insert(out.indices.end(), { center, r0, r1 });
			}
		}
		};

	if (params.cap == PrimitiveCylinderCap::Top || params.cap == PrimitiveCylinderCap::Both) {
		buildCap(halfHeight, topRadius, Vector3(0.0f, 1.0f, 0.0f), false);
	}
	if (params.cap == PrimitiveCylinderCap::Bottom || params.cap == PrimitiveCylinderCap::Both) {
		buildCap(-halfHeight, bottomRadius, Vector3(0.0f, -1.0f, 0.0f), true);
	}
}

void Engine::PrimitiveMeshGenerator::GenerateHemisphere(const PrimitiveHemisphereParams& params, PrimitiveMeshData& out) {

	const int32_t longitude = std::clamp(params.longitudeDivide, 3, kMaxPrimitiveDivide);
	const int32_t latitude = std::clamp(params.latitudeDivide, 1, kMaxPrimitiveDivide);
	const float radius = params.radius;
	constexpr float pi = std::numbers::pi_v<float>;

	// 上半球、thetaを0からπ/2まで
	for (int32_t y = 0; y <= latitude; ++y) {

		const float v = static_cast<float>(y) / static_cast<float>(latitude);
		const float theta = pi * 0.5f * v;
		const float sinTheta = std::sin(theta);
		const float cosTheta = std::cos(theta);

		for (int32_t x = 0; x <= longitude; ++x) {

			const float u = static_cast<float>(x) / static_cast<float>(longitude);
			const float phi = pi * 2.0f * u;
			const float sinPhi = std::sin(phi);
			const float cosPhi = std::cos(phi);

			PrimitiveMeshVertex vertex{};
			vertex.normal = Vector3(sinTheta * cosPhi, cosTheta, sinTheta * sinPhi);
			vertex.position = Vector3(vertex.normal.x * radius, vertex.normal.y * radius, vertex.normal.z * radius);
			vertex.texcoord = Vector2(u, v);
			out.vertices.push_back(vertex);
		}
	}

	const int32_t stride = longitude + 1;
	for (int32_t y = 0; y < latitude; ++y) {
		for (int32_t x = 0; x < longitude; ++x) {

			const uint32_t i0 = static_cast<uint32_t>(y * stride + x);
			const uint32_t i1 = i0 + 1;
			const uint32_t i2 = i0 + static_cast<uint32_t>(stride);
			const uint32_t i3 = i2 + 1;
			out.indices.insert(out.indices.end(), { i0, i2, i1, i1, i2, i3 });
		}
	}

	// 底面のフタ、赤道リングと中心で扇状に張る
	if (!params.bottomCap) {
		return;
	}
	const uint32_t center = static_cast<uint32_t>(out.vertices.size());
	PrimitiveMeshVertex centerVertex{};
	centerVertex.position = Vector3(0.0f, 0.0f, 0.0f);
	centerVertex.normal = Vector3(0.0f, -1.0f, 0.0f);
	centerVertex.texcoord = Vector2(0.5f, 0.5f);
	out.vertices.push_back(centerVertex);

	for (int32_t x = 0; x <= longitude; ++x) {

		const float u = static_cast<float>(x) / static_cast<float>(longitude);
		const float phi = pi * 2.0f * u;
		const float sinPhi = std::sin(phi);
		const float cosPhi = std::cos(phi);
		PrimitiveMeshVertex vertex{};
		vertex.position = Vector3(cosPhi * radius, 0.0f, sinPhi * radius);
		vertex.normal = Vector3(0.0f, -1.0f, 0.0f);
		vertex.texcoord = Vector2(cosPhi * 0.5f + 0.5f, sinPhi * 0.5f + 0.5f);
		out.vertices.push_back(vertex);
	}
	for (int32_t x = 0; x < longitude; ++x) {

		const uint32_t r0 = center + 1 + static_cast<uint32_t>(x);
		const uint32_t r1 = r0 + 1;
		out.indices.insert(out.indices.end(), { center, r1, r0 });
	}
}

void Engine::PrimitiveMeshGenerator::GenerateCube(const PrimitiveCubeParams& params, PrimitiveMeshData& out) {

	const float halfX = params.size.x * 0.5f;
	const float halfY = params.size.y * 0.5f;
	const float halfZ = params.size.z * 0.5f;
	// pivotを原点に合わせるためのオフセット
	const Vector3 offset(
		Lerp(-halfX, halfX, params.pivot.x),
		Lerp(-halfY, halfY, params.pivot.y),
		Lerp(-halfZ, halfZ, params.pivot.z));

	// 面ごとに法線とUの向きとVの向きを持つ、面ごとに頂点を分けて法線を立てる
	struct Face {

		Vector3 normal;
		Vector3 uAxis;
		Vector3 vAxis;
	};
	const Face faces[6] = {
		{ Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 1.0f, 0.0f) },
		{ Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f), Vector3(0.0f, 1.0f, 0.0f) },
		{ Vector3(0.0f, 1.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f) },
		{ Vector3(0.0f, -1.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, -1.0f) },
		{ Vector3(0.0f, 0.0f, 1.0f), Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f) },
		{ Vector3(0.0f, 0.0f, -1.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f) },
	};

	// 軸方向の半径を辺の大きさから取る、軸は単位でどれか1成分だけ立っている
	const auto halfAlong = [&](const Vector3& axis) {
		return std::abs(axis.x) * halfX + std::abs(axis.y) * halfY + std::abs(axis.z) * halfZ;
		};

	const float corners[4][2] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { 1.0f, 1.0f } };
	for (const Face& face : faces) {

		const Vector3 center = face.normal * halfAlong(face.normal);
		const float halfU = halfAlong(face.uAxis);
		const float halfV = halfAlong(face.vAxis);
		const uint32_t base = static_cast<uint32_t>(out.vertices.size());

		for (int32_t c = 0; c < 4; ++c) {

			const float uu = corners[c][0];
			const float vv = corners[c][1];

			PrimitiveMeshVertex vertex{};
			vertex.position = center + face.uAxis * (uu * halfU) + face.vAxis * (vv * halfV) - offset;
			vertex.normal = face.normal;
			vertex.texcoord = Vector2(uu * 0.5f + 0.5f, 1.0f - (vv * 0.5f + 0.5f));
			out.vertices.push_back(vertex);
		}
		out.indices.insert(out.indices.end(),
			{ base + 0, base + 2, base + 1, base + 1, base + 2, base + 3 });
	}
}