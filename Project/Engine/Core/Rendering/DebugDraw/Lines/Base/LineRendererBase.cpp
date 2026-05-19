#include "LineRendererBase.h"

//============================================================================
//	LineRendererBase classMethods
//============================================================================

template <>
void Engine::LineRendererBase<Engine::Vector2>::DrawLine(const Vector2& pointA,
	const Vector2& pointB, const Color4& color, float thickness) {

	DrawLine(pointA, color, thickness, pointB, color, thickness);
}
template <>
void Engine::LineRendererBase<Engine::Vector3>::DrawLine(const Vector3& pointA,
	const Vector3& pointB, const Color4& color, float thickness) {

	DrawLine(pointA, color, thickness, pointB, color, thickness);
}

template <>
void Engine::LineRendererBase<Engine::Vector2>::DrawLine(const Vector2& pointA, const Color4& colorA,
	float thicknessA, const Vector2& pointB, const Color4& colorB, float thicknessB) {

	// 頂点数が最大値を超えるなら追加しない
	if (kMaxLineCount_ <= GetCurrentLineCount()) {
		return;
	}
	Vector3 pointA3D = Vector3(pointA.x, pointA.y, 0.0f);
	Vector3 pointB3D = Vector3(pointB.x, pointB.y, 0.0f);
	vertices_.push_back({ pointA3D, thicknessA, colorA });
	vertices_.push_back({ pointB3D, thicknessB, colorB });
}
template <>
void Engine::LineRendererBase<Engine::Vector3>::DrawLine(const Vector3& pointA, const Color4& colorA,
	float thicknessA, const Vector3& pointB, const Color4& colorB, float thicknessB) {

	// 頂点数が最大値を超えるなら追加しない
	if (kMaxLineCount_ <= GetCurrentLineCount()) {
		return;
	}
	vertices_.push_back({ pointA, thicknessA, colorA });
	vertices_.push_back({ pointB, thicknessB, colorB });
}