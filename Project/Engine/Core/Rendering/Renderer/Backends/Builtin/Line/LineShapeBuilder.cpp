#include "LineShapeBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <cmath>

//============================================================================
//	LineShapeBuilder internal
//============================================================================
namespace {

	using Engine::LinePoint;
	using Engine::Vector3;
	using Engine::Color4;

	// 1線分を2点として積む
	void PushLine(const Vector3& a, const Vector3& b, const Color4& color, float thickness, std::vector<LinePoint>& out) {

		out.emplace_back(LinePoint{ a, color, thickness });
		out.emplace_back(LinePoint{ b, color, thickness });
	}

	// 分割数の下限を確保する
	uint32_t SafeDivision(uint32_t division) {
		return division < 3 ? 3u : division;
	}
}

//============================================================================
//	LineShapeBuilder namespaceMethods
//============================================================================
void Engine::LineShapeBuilder::BuildSphere(const Vector3& center, float radius, const Color4& color,
	uint32_t division, float thickness, std::vector<LinePoint>& out) {

	const uint32_t latDivision = SafeDivision(division);
	const float kLatEvery = Math::pi / static_cast<float>(latDivision);
	const float kLonEvery = 2.0f * Math::pi / static_cast<float>(latDivision);

	auto calcPoint = [&](float lat, float lon) -> Vector3 {
		return {
			radius * std::cos(lat) * std::cos(lon),
			radius * std::sin(lat),
			radius * std::cos(lat) * std::sin(lon)
		};
		};

	for (uint32_t latIndex = 0; latIndex < latDivision; ++latIndex) {

		const float lat = -Math::pi / 2.0f + kLatEvery * static_cast<float>(latIndex);
		for (uint32_t lonIndex = 0; lonIndex < latDivision; ++lonIndex) {

			const float lon = static_cast<float>(lonIndex) * kLonEvery;
			const Vector3 pointA = calcPoint(lat, lon);
			const Vector3 pointB = calcPoint(lat + kLatEvery, lon);
			const Vector3 pointC = calcPoint(lat, lon + kLonEvery);

			PushLine(center + pointA, center + pointB, color, thickness, out);
			PushLine(center + pointA, center + pointC, color, thickness, out);
		}
	}
}

void Engine::LineShapeBuilder::BuildHemisphere(const Vector3& center, float radius, const Quaternion& rotation,
	const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out) {

	const uint32_t div = SafeDivision(division);
	const float kLatEvery = (Math::pi / 2.0f) / static_cast<float>(div);
	const float kLonEvery = 2.0f * Math::pi / static_cast<float>(div);
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);

	auto calcPoint = [&](float lat, float lon) -> Vector3 {
		return {
			radius * std::cos(lat) * std::cos(lon),
			radius * std::sin(lat),
			radius * std::cos(lat) * std::sin(lon)
		};
		};

	for (uint32_t latIndex = 0; latIndex < div; ++latIndex) {

		const float lat = kLatEvery * static_cast<float>(latIndex);
		for (uint32_t lonIndex = 0; lonIndex < div; ++lonIndex) {

			const float lon = static_cast<float>(lonIndex) * kLonEvery;
			Vector3 pointA = Vector3::TransformPoint(calcPoint(lat, lon), rotationMatrix) + center;
			Vector3 pointB = Vector3::TransformPoint(calcPoint(lat + kLatEvery, lon), rotationMatrix) + center;
			Vector3 pointC = Vector3::TransformPoint(calcPoint(lat, lon + kLonEvery), rotationMatrix) + center;

			PushLine(pointA, pointB, color, thickness, out);
			PushLine(pointA, pointC, color, thickness, out);
		}
	}
}

void Engine::LineShapeBuilder::BuildAABB(const Vector3& min, const Vector3& max, const Color4& color,
	float thickness, std::vector<LinePoint>& out) {

	const Vector3 center = (min + max) * 0.5f;
	const Vector3 half = (max - min) * 0.5f;
	BuildOBB(center, half, Quaternion::Identity(), color, thickness, out);
}

void Engine::LineShapeBuilder::BuildOBB(const Vector3& center, const Vector3& size, const Quaternion& rotation,
	const Color4& color, float thickness, std::vector<LinePoint>& out) {

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Vector3 halfX = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix) * size.x;
	const Vector3 halfY = Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix) * size.y;
	const Vector3 halfZ = Vector3::Transform(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix) * size.z;

	const Vector3 offsets[8] = {
		{-1.0f, -1.0f, -1.0f}, {-1.0f,  1.0f, -1.0f}, {1.0f, -1.0f, -1.0f}, {1.0f,  1.0f, -1.0f},
		{-1.0f, -1.0f,  1.0f}, {-1.0f,  1.0f,  1.0f}, {1.0f, -1.0f,  1.0f}, {1.0f,  1.0f,  1.0f}
	};
	Vector3 vertices[8];
	for (int i = 0; i < 8; ++i) {
		vertices[i] = center + offsets[i].x * halfX + offsets[i].y * halfY + offsets[i].z * halfZ;
	}

	const int edges[12][2] = {
		{0, 1}, {1, 3}, {3, 2}, {2, 0},
		{4, 5}, {5, 7}, {7, 6}, {6, 4},
		{0, 4}, {1, 5}, {2, 6}, {3, 7}
	};
	for (int i = 0; i < 12; ++i) {
		PushLine(vertices[edges[i][0]], vertices[edges[i][1]], color, thickness, out);
	}
}

void Engine::LineShapeBuilder::BuildCone(const Vector3& center, float baseRadius, float topRadius, float height,
	const Quaternion& rotation, const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out) {

	const uint32_t div = SafeDivision(division);
	const float kAngleStep = 2.0f * Math::pi / static_cast<float>(div);
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);

	for (uint32_t i = 0; i < div; ++i) {

		const float angle0 = static_cast<float>(i) * kAngleStep;
		const float angle1 = static_cast<float>(i + 1) * kAngleStep;

		const Vector3 base0 = Vector3::TransformPoint(Vector3(baseRadius * std::cos(angle0), 0.0f, baseRadius * std::sin(angle0)), rotationMatrix) + center;
		const Vector3 base1 = Vector3::TransformPoint(Vector3(baseRadius * std::cos(angle1), 0.0f, baseRadius * std::sin(angle1)), rotationMatrix) + center;
		const Vector3 top0 = Vector3::TransformPoint(Vector3(topRadius * std::cos(angle0), height, topRadius * std::sin(angle0)), rotationMatrix) + center;
		const Vector3 top1 = Vector3::TransformPoint(Vector3(topRadius * std::cos(angle1), height, topRadius * std::sin(angle1)), rotationMatrix) + center;

		PushLine(base0, base1, color, thickness, out);
		PushLine(top0, top1, color, thickness, out);
		PushLine(base0, top0, color, thickness, out);
	}
}

void Engine::LineShapeBuilder::BuildArrow(const Vector3& pos, float length, const Quaternion& rotation,
	const Color4& color, float thickness, std::vector<LinePoint>& out) {

	if (length <= 0.0f) {
		return;
	}

	constexpr uint32_t kDivision = 16;
	constexpr uint32_t kAxisCount = 4;
	const float shaftLength = length * 0.72f;
	const float shaftRadius = length * 0.035f;
	const float headRadius = length * 0.11f;

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Vector3 right = Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix), Vector3(1.0f, 0.0f, 0.0f));
	const Vector3 up = Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix), Vector3(0.0f, 1.0f, 0.0f));
	const Vector3 forward = Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix), Vector3(0.0f, 0.0f, 1.0f));

	auto makePoint = [&](float y, float radius, float angle) -> Vector3 {
		return pos + up * y + right * (std::cos(angle) * radius) + forward * (std::sin(angle) * radius);
		};

	const float kEvery = 2.0f * Math::pi / static_cast<float>(kDivision);
	// 円柱部の上下輪郭円
	for (uint32_t i = 0; i < kDivision; ++i) {

		const float angle0 = kEvery * static_cast<float>(i);
		const float angle1 = kEvery * static_cast<float>(i + 1);
		PushLine(makePoint(0.0f, shaftRadius, angle0), makePoint(0.0f, shaftRadius, angle1), color, thickness, out);
		PushLine(makePoint(shaftLength, shaftRadius, angle0), makePoint(shaftLength, shaftRadius, angle1), color, thickness, out);
	}
	// 円柱部の側面線
	for (uint32_t i = 0; i < kAxisCount; ++i) {

		const float angle = (2.0f * Math::pi / static_cast<float>(kAxisCount)) * static_cast<float>(i);
		PushLine(makePoint(0.0f, shaftRadius, angle), makePoint(shaftLength, shaftRadius, angle), color, thickness, out);
	}
	// 先端の円錐部
	const Vector3 tip = pos + up * length;
	for (uint32_t i = 0; i < kDivision; ++i) {

		const float angle0 = kEvery * static_cast<float>(i);
		const float angle1 = kEvery * static_cast<float>(i + 1);
		PushLine(makePoint(shaftLength, headRadius, angle0), makePoint(shaftLength, headRadius, angle1), color, thickness, out);
	}
	for (uint32_t i = 0; i < kAxisCount; ++i) {

		const float angle = (2.0f * Math::pi / static_cast<float>(kAxisCount)) * static_cast<float>(i);
		PushLine(makePoint(shaftLength, headRadius, angle), tip, color, thickness, out);
	}
}

void Engine::LineShapeBuilder::BuildAxis(const Vector3& pos, const Quaternion& rotation, float length,
	float thickness, std::vector<LinePoint>& out) {

	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);
	const Vector3 xDirection = Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotationMatrix).Normalize();
	const Vector3 yDirection = Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotationMatrix).Normalize();
	const Vector3 zDirection = Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotationMatrix).Normalize();

	// X赤Y青Z緑、デバッグ軸表示と同じ色対応
	PushLine(pos, pos + xDirection * length, Color4::Red(), thickness, out);
	PushLine(pos, pos + yDirection * length, Color4::Blue(), thickness, out);
	PushLine(pos, pos + zDirection * length, Color4::Green(), thickness, out);
}

void Engine::LineShapeBuilder::BuildCircle2D(const Vector2& center, float radius, const Color4& color,
	uint32_t division, float thickness, std::vector<LinePoint>& out) {

	const uint32_t div = SafeDivision(division);
	const float kAngleStep = 2.0f * Math::pi / static_cast<float>(div);

	auto calcPoint = [&](uint32_t index) -> Vector3 {
		const float angle = static_cast<float>(index) * kAngleStep;
		return { center.x + radius * std::cos(angle), center.y + radius * std::sin(angle), 0.0f };
		};
	for (uint32_t i = 0; i < div; ++i) {
		PushLine(calcPoint(i), calcPoint((i + 1) % div), color, thickness, out);
	}
}

void Engine::LineShapeBuilder::BuildRect2D(const Vector2& center, const Vector2& size, const Quaternion& rotation,
	const Color4& color, float thickness, std::vector<LinePoint>& out) {

	const Vector2 half = size * 0.5f;
	const Matrix4x4 rotationMatrix = Quaternion::MakeRotateMatrix(rotation);

	// ローカル角を回転してワールドへ置く、2Dなのでz成分は描画時に潰れる
	auto corner = [&](float signX, float signY) -> Vector3 {
		const Vector3 local(signX * half.x, signY * half.y, 0.0f);
		const Vector3 rotated = Vector3::Transform(local, rotationMatrix);
		return { center.x + rotated.x, center.y + rotated.y, 0.0f };
		};

	const Vector3 corners[4] = {
		corner(-1.0f, -1.0f), corner(1.0f, -1.0f), corner(1.0f, 1.0f), corner(-1.0f, 1.0f)
	};
	for (int i = 0; i < 4; ++i) {
		PushLine(corners[i], corners[(i + 1) % 4], color, thickness, out);
	}
}
