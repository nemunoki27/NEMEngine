#include "PrimitiveMeshGenerator.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
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
	int32_t heightDivide = std::clamp(params.heightDivide, 2, kMaxPrimitiveDivide);
	if ((heightDivide & 1) != 0 && heightDivide < kMaxPrimitiveDivide) {
		++heightDivide;
	}
	const float topRadius = params.topRadius;
	const float centerRadius = params.centerRadius;
	const float bottomRadius = params.bottomRadius;
	const float height = params.height;
	const float halfHeight = height * 0.5f;
	constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
	const float angleStep = params.maxAngle * degToRad / static_cast<float>(radialDivide);
	const auto evaluateRadius = [&](float t) {

		if (t <= 0.5f) {

			const float localT = std::clamp(t * 2.0f, 0.0f, 1.0f);
			const float smoothT = localT * localT * (3.0f - 2.0f * localT);
			const float weightedT = std::pow(smoothT, 1.0f + std::clamp(params.bottomRadiusWeight, 0.0f, 1.0f) * 3.0f);
			return Lerp(bottomRadius, centerRadius, weightedT);
		}

		const float localT = std::clamp((t - 0.5f) * 2.0f, 0.0f, 1.0f);
		const float smoothT = localT * localT * (3.0f - 2.0f * localT);
		const float weightedT = 1.0f - std::pow(1.0f - smoothT,
			1.0f + std::clamp(params.topRadiusWeight, 0.0f, 1.0f) * 3.0f);
		return Lerp(centerRadius, topRadius, weightedT);
		};

	// 側面、高さと円周のグリッド、中心はローカル原点
	for (int32_t h = 0; h <= heightDivide; ++h) {

		const float th = static_cast<float>(h) / static_cast<float>(heightDivide);
		const float radius = evaluateRadius(th);
		const float y = Lerp(-halfHeight, halfHeight, th);
		const float prevT = std::clamp(th - 0.001f, 0.0f, 1.0f);
		const float nextT = std::clamp(th + 0.001f, 0.0f, 1.0f);
		const float deltaY = (nextT - prevT) * height;
		const float radiusSlope = deltaY != 0.0f ?
			(evaluateRadius(nextT) - evaluateRadius(prevT)) / deltaY : 0.0f;

		for (int32_t i = 0; i <= radialDivide; ++i) {

			const float angle = angleStep * static_cast<float>(i);
			const float sinA = std::sin(angle);
			const float cosA = std::cos(angle);

			PrimitiveMeshVertex vertex{};
			vertex.position = Vector3(cosA * radius, y, sinA * radius);
			vertex.normal = Vector3::Normalize(Vector3(cosA, -radiusSlope, sinA));
			if (params.uvMode == PrimitiveCylinderUVMode::Radial) {

				const float uvRadius = th * 0.5f;
				vertex.texcoord = Vector2(
					cosA * uvRadius + 0.5f,
					-sinA * uvRadius + 0.5f);
			} else {

				vertex.texcoord = Vector2(
					static_cast<float>(i) / static_cast<float>(radialDivide),
					1.0f - th);
			}
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
