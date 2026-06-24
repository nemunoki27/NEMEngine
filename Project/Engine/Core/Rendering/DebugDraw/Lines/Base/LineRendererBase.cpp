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

	// 積み先バッチを取得し、頂点数が最大値を超えるなら追加しない
	std::vector<LineVertex>& target = ActiveVertices();
	if (kMaxLineCount_ <= static_cast<uint32_t>(target.size() / 2)) {
		return;
	}
	Vector3 pointA3D = Vector3(pointA.x, pointA.y, 0.0f);
	Vector3 pointB3D = Vector3(pointB.x, pointB.y, 0.0f);
	target.push_back({ pointA3D, thicknessA, colorA });
	target.push_back({ pointB3D, thicknessB, colorB });
}
template <>
void Engine::LineRendererBase<Engine::Vector3>::DrawLine(const Vector3& pointA, const Color4& colorA,
	float thicknessA, const Vector3& pointB, const Color4& colorB, float thicknessB) {

	// 積み先バッチを取得し、頂点数が最大値を超えるなら追加しない
	std::vector<LineVertex>& target = ActiveVertices();
	if (kMaxLineCount_ <= static_cast<uint32_t>(target.size() / 2)) {
		return;
	}
	target.push_back({ pointA, thicknessA, colorA });
	target.push_back({ pointB, thicknessB, colorB });
}