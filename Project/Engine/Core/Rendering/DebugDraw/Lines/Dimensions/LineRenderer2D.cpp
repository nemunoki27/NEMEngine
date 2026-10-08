#include "LineRenderer2D.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Line/LineShapeBuilder.h>

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

	DrawRect(center, size, anchor, 0.0f, color, thickness);
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center,
	const Vector2& size, const Color4& color, float thickness) {

	DrawRect(center, size, Vector2::AnyInit(0.5f), color, thickness);
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center, const Vector2& size,
	const Vector2& anchor, float rotationDegrees, const Color4& color, float thickness) {

	// 回転角と半径を共通の矩形生成へ渡す
	const Matrix4x4 rotation = Matrix4x4::MakeRotateMatrix(Vector3(0.0f, 0.0f, rotationDegrees));
	LineShapeBuilder::ForEachRect2DLine(center, size * anchor, rotation,
		[&](const Vector2& start, const Vector2& end) {
			DrawLine(start, end, color, thickness);
		});
}

void Engine::LineRenderer2D::DrawRect(const Vector2& center,
	const Vector2& size, float rotationDegrees, const Color4& color, float thickness) {

	DrawRect(center, size, Vector2::AnyInit(0.5f), rotationDegrees, color, thickness);
}

void Engine::LineRenderer2D::DrawCircle(const Vector2& center, float radius,
	const Color4& color, uint32_t division, float thickness) {

	// 共通の円周から描画用の線を追加
	LineShapeBuilder::ForEachCircle2DLine(center, radius, division,
		[&](const Vector2& start, const Vector2& end) {
			DrawLine(start, end, color, thickness);
		});
}

void Engine::LineRenderer2D::DrawLineImpl([[maybe_unused]] GraphicsCore& graphicsCore,
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

	// Camera行列から画面に映る範囲を取得
	const Matrix4x4 invViewProjection = camera->matrices.inverseProjectionMatrix * camera->matrices.inverseViewMatrix;
	const Vector3 corner0 = Vector3::Transform(Vector3(-1.0f, -1.0f, 0.0f), invViewProjection);
	const Vector3 corner1 = Vector3::Transform(Vector3(1.0f, -1.0f, 0.0f), invViewProjection);
	const Vector3 corner2 = Vector3::Transform(Vector3(-1.0f, 1.0f, 0.0f), invViewProjection);
	const Vector3 corner3 = Vector3::Transform(Vector3(1.0f, 1.0f, 0.0f), invViewProjection);
	const float minX = (std::min)({ corner0.x, corner1.x, corner2.x, corner3.x });
	const float maxX = (std::max)({ corner0.x, corner1.x, corner2.x, corner3.x });
	const float minY = (std::min)({ corner0.y, corner1.y, corner2.y, corner3.y });
	const float maxY = (std::max)({ corner0.y, corner1.y, corner2.y, corner3.y });

	// 密集したグリッドの間隔を広げる
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
