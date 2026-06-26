#include "LineRenderer2D.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	LineRenderer2D classMethods
//============================================================================
Engine::LineRenderer2D::LineRenderer2D(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain) {

	// 基底クラスの初期化
	Init(graphicsCore, cameraDomain);
}

void Engine::LineRenderer2D::BeginFrame() {

	LineRendererBase<Vector2>::BeginFrame();
	gridDrawCount_ = 0;
	gridCellSize_ = 0.0f;
}

void Engine::LineRenderer2D::DrawGrid(float cellSize) {

	++gridDrawCount_;
	gridCellSize_ = cellSize;
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center, const Vector2& size,
	const Vector2& anchor, const Color4& color, float thickness) {

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
	const Vector2& size, const Color4& color, float thickness) {

	DrawRect(center, size, Vector2::AnyInit(0.5f), color, thickness);
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center, const Vector2& size,
	const Vector2& anchor, float rotationDegrees, const Color4& color, float thickness) {

	// サイズ
	Vector2 rectSize = size * anchor;

	const float rotationRadians = Math::DegToRad(rotationDegrees);
	const float cos = std::cos(rotationRadians);
	const float sin = std::sin(rotationRadians);

	const Vector2 axisX(cos, sin);
	const Vector2 axisY(-sin, cos);
	const Vector2 halfX = axisX * rectSize.x;
	const Vector2 halfY = axisY * rectSize.y;

	// 4頂点座標計算
	Vector2 topLeft = center - halfX - halfY;
	Vector2 topRight = center + halfX - halfY;
	Vector2 bottomRight = center + halfX + halfY;
	Vector2 bottomLeft = center - halfX + halfY;

	// 4辺のライン描画
	DrawLine(topLeft, topRight, color, thickness);
	DrawLine(topRight, bottomRight, color, thickness);
	DrawLine(bottomRight, bottomLeft, color, thickness);
	DrawLine(bottomLeft, topLeft, color, thickness);
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center,
	const Vector2& size, float rotationDegrees, const Color4& color, float thickness) {

	DrawRect(center, size, Vector2::AnyInit(0.5f), rotationDegrees, color, thickness);
}

void Engine::LineRenderer2D::DrawCircle(const Vector2& center, float radius,
	const Color4& color, uint32_t division, float thickness) {

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
	const ResolvedCameraView* camera, MultiRenderTarget& surface) {

	if (gridDrawCount_ == 0) {
		return;
	}
	if (!camera) {

		gridDrawCount_ = 0;
		gridCellSize_ = 0.0f;
		return;
	}

	constexpr float kDefaultGridCellSize = 64.0f;
	// DrawGridで指定があればそのセル幅、無ければ既定値を使う
	const float baseGridCellSize = gridCellSize_ > 0.0f ? gridCellSize_ : kDefaultGridCellSize;
	constexpr float kGridLineThickness = 1.0f;
	constexpr float kGridCenterLineThickness = 2.4f;
	const Color4 kGridColor = Color4::White(0.32f);

	// 逆ViewProjで画面に映るワールド矩形を求める、2Dは1ワールド=1pxとは限らないのでカメラから求める
	const Matrix4x4 invViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
	const Vector3 corner0 = Vector3::Transform(Vector3(-1.0f, -1.0f, 0.0f), invViewProjection);
	const Vector3 corner1 = Vector3::Transform(Vector3(1.0f, -1.0f, 0.0f), invViewProjection);
	const Vector3 corner2 = Vector3::Transform(Vector3(-1.0f, 1.0f, 0.0f), invViewProjection);
	const Vector3 corner3 = Vector3::Transform(Vector3(1.0f, 1.0f, 0.0f), invViewProjection);
	const float minX = (std::min)({ corner0.x, corner1.x, corner2.x, corner3.x });
	const float maxX = (std::max)({ corner0.x, corner1.x, corner2.x, corner3.x });
	const float minY = (std::min)({ corner0.y, corner1.y, corner2.y, corner3.y });
	const float maxY = (std::max)({ corner0.y, corner1.y, corner2.y, corner3.y });

	// セルが画面上で細かすぎると線が潰れて真っ白になるので、最小ピクセル間隔まで2倍ずつ粗くする
	constexpr float kMinCellPixels = 8.0f;
	const float pixelsPerUnit = static_cast<float>(surface.GetWidth()) / (std::max)(maxX - minX, 0.0001f);
	float gridCellSize = baseGridCellSize;
	for (int guard = 0; pixelsPerUnit > 0.0f && gridCellSize * pixelsPerUnit < kMinCellPixels && guard < 64; ++guard) {
		gridCellSize *= 2.0f;
	}

	const float left = std::floor(minX / gridCellSize) * gridCellSize - gridCellSize;
	const float top = std::floor(minY / gridCellSize) * gridCellSize - gridCellSize;
	const float right = maxX + gridCellSize;
	const float bottom = maxY + gridCellSize;

	// 縦線
	for (float x = left; x <= right; x += gridCellSize) {

		const float thickness = std::abs(x) < 0.001f ? kGridCenterLineThickness : kGridLineThickness;
		DrawLine(Vector2(x, top), Vector2(x, bottom), kGridColor, thickness);
	}
	// 横線
	for (float y = top; y <= bottom; y += gridCellSize) {

		const float thickness = std::abs(y) < 0.001f ? kGridCenterLineThickness : kGridLineThickness;
		DrawLine(Vector2(left, y), Vector2(right, y), kGridColor, thickness);
	}

	gridDrawCount_ = 0;
	gridCellSize_ = 0.0f;
}
