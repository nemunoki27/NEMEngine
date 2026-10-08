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

	using Engine::Color4;
	using Engine::LinePoint;
	using Engine::Vector3;

	// 1線分を2点として積む
	void PushLine(const Vector3& a, const Vector3& b, const Color4& color, float thickness, std::vector<LinePoint>& out) {

		out.emplace_back(LinePoint{a, color, thickness});
		out.emplace_back(LinePoint{b, color, thickness});
	}

}

namespace Engine::LineShapeBuilder::Detail {

	// 緯度と経度から球面上の点を取得
	Vector3 SphericalPoint(float radius, float latitude, float longitude) {

		return {radius * std::cos(latitude) * std::cos(longitude), radius * std::sin(latitude),
			radius * std::cos(latitude) * std::sin(longitude)};
	}

	// 分割数を三つ以上に揃える
	uint32_t SafeDivision(uint32_t division) {
		return division < 3 ? 3u : division;
	}
}

//============================================================================
//	LineShapeBuilder namespaceMethods
//============================================================================
void Engine::LineShapeBuilder::BuildSphere(
	const Vector3& center, float radius, const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out) {

	// 共通の形状をComponentの点列へ追加
	ForEachSphereLine(center, radius, division,
		[&](const Vector3& start, const Vector3& end) {
			PushLine(start, end, color, thickness, out);
		});
}

void Engine::LineShapeBuilder::BuildHemisphere(const Vector3& center, float radius, const Quaternion& rotation,
	const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out) {

	// 共通の形状をComponentの点列へ追加
	ForEachHemisphereLine(center, radius, Quaternion::MakeRotateMatrix(rotation), division,
		[&](const Vector3& start, const Vector3& end) {
			PushLine(start, end, color, thickness, out);
		});
}

void Engine::LineShapeBuilder::BuildAABB(
	const Vector3& min, const Vector3& max, const Color4& color, float thickness, std::vector<LinePoint>& out) {

	// 最小点と最大点を中心と半径へ変換
	const Vector3 center = (min + max) * 0.5f;
	const Vector3 half = (max - min) * 0.5f;
	BuildOBB(center, half, Quaternion::Identity(), color, thickness, out);
}

void Engine::LineShapeBuilder::BuildOBB(const Vector3& center, const Vector3& size, const Quaternion& rotation,
	const Color4& color, float thickness, std::vector<LinePoint>& out) {

	// 共通の形状をComponentの点列へ追加
	ForEachOBBLine(center, size, Quaternion::MakeRotateMatrix(rotation),
		[&](const Vector3& start, const Vector3& end) {
			PushLine(start, end, color, thickness, out);
		});
}

void Engine::LineShapeBuilder::BuildCone(const Vector3& center, float baseRadius, float topRadius, float height,
	const Quaternion& rotation, const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out) {

	// 共通の形状をComponentの点列へ追加
	ForEachConeLine(center, baseRadius, topRadius, height, Quaternion::MakeRotateMatrix(rotation), division,
		[&](const Vector3& start, const Vector3& end) {
			PushLine(start, end, color, thickness, out);
		});
}

void Engine::LineShapeBuilder::BuildArrow(const Vector3& pos, float length, const Quaternion& rotation, const Color4& color,
	float thickness, std::vector<LinePoint>& out) {

	// 描画と同じ形状をComponentの点列へ追加
	ForEachArrowLine(pos, length, Quaternion::MakeRotateMatrix(rotation),
		[&](const Vector3& start, const Vector3& end) {
			PushLine(start, end, color, thickness, out);
		});
}

void Engine::LineShapeBuilder::BuildAxis(
	const Vector3& pos, const Quaternion& rotation, float length, float thickness, std::vector<LinePoint>& out) {

	// 描画と同じ形状をComponentの点列へ追加
	ForEachAxisLine(pos, length, Quaternion::MakeRotateMatrix(rotation),
		[&](const Vector3& start, const Vector3& end, const Color4& lineColor) {
			PushLine(start, end, lineColor, thickness, out);
		});
}

void Engine::LineShapeBuilder::BuildCircle2D(
	const Vector2& center, float radius, const Color4& color, uint32_t division, float thickness, std::vector<LinePoint>& out) {

	// 共通の平面形状を点列へ追加
	ForEachCircle2DLine(center, radius, division,
		[&](const Vector2& start, const Vector2& end) {
			PushLine(Vector3(start.x, start.y, 0.0f), Vector3(end.x, end.y, 0.0f), color, thickness, out);
		});
}

void Engine::LineShapeBuilder::BuildRect2D(const Vector2& center, const Vector2& size, const Quaternion& rotation,
	const Color4& color, float thickness, std::vector<LinePoint>& out) {

	// 共通の平面形状を点列へ追加
	ForEachRect2DLine(center, size * 0.5f, Quaternion::MakeRotateMatrix(rotation),
		[&](const Vector2& start, const Vector2& end) {
			PushLine(Vector3(start.x, start.y, 0.0f), Vector3(end.x, end.y, 0.0f), color, thickness, out);
		});
}
