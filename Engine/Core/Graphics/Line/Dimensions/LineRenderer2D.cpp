#include "LineRenderer2D.h"

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Math.h>

//============================================================================
//	LineRenderer2D classMethods
//============================================================================

Engine::LineRenderer2D::LineRenderer2D(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain) {

	// 基底クラスの初期化
	Init(graphicsCore, cameraDomain);
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center, const Vector2& size,
	const Vector2& anchor, const Color& color, float thickness) {

	// サイズ
	Vector2 rectSize = size * anchor;

	// 4頂点座標計算
	Vector2 topLeft = Vector2(center.x - rectSize.x, center.y - rectSize.y);
	Vector2 topRight = Vector2(center.x + rectSize.x, center.y - rectSize.y);
	Vector2 bottomLeft = Vector2(center.x - rectSize.x, center.y + rectSize.y);
	Vector2 bottomRight = Vector2(center.x + rectSize.x, center.y + rectSize.y);

	// 4辺のライン描画
	DrawLine(topLeft, topRight, color, thickness);
	DrawLine(topRight, bottomRight, color, thickness);
	DrawLine(bottomRight, bottomLeft, color, thickness);
	DrawLine(bottomLeft, topLeft, color, thickness);
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center,
	const Vector2& size, const Color& color, float thickness) {

	DrawRect(center, size, Vector2::AnyInit(0.5f), color, thickness);
}

void Engine::LineRenderer2D::DrawCircle(const Vector2& center, float radius,
	const Color& color, uint32_t division, float thickness) {

	// 必ず3以上
	uint32_t actualDivision = (std::max)(division, static_cast<uint32_t>(3));

	for (uint32_t i = 0; i < actualDivision; ++i) {

		// 円周上の点の座標計算
		float angle0 = Math::pi * 2.0f * (static_cast<float>(i) / actualDivision);
		float angle1 = Math::pi * 2.0f * (static_cast<float>(i + 1) / actualDivision);

		Vector2 pointA = Vector2(center.x + std::cos(angle0) * radius, center.y + std::sin(angle0) * radius);
		Vector2 pointB = Vector2(center.x + std::cos(angle1) * radius, center.y + std::sin(angle1) * radius);

		DrawLine(pointA, pointB, color, thickness);
	}
}

void Engine::LineRenderer2D::DrawLineImpl(GraphicsCore& /*graphicsCore*/,
	const ResolvedCameraView* /*camera*/, MultiRenderTarget& /*surface*/) {
}