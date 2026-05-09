#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Line/Base/LineRendererBase.h>

namespace Engine {

	//============================================================================
	//	LineRenderer2D class
	//	2Dライン描画を行うクラス
	//============================================================================
	class LineRenderer2D :
		public LineRendererBase<Vector2> {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		LineRenderer2D(GraphicsCore& graphicsCore, RenderCameraDomain cameraDomain);
		~LineRenderer2D() = default;

		// フレーム開始処理
		void BeginFrame();

		//---------- drawers -----------------------------------------------------

		// グリッド描画
		void DrawGrid();

		// アンカー付き/無し矩形
		void DrawRect(const Vector2& center, const Vector2& size, const Vector2& anchor, const Color4& color, float thickness = 1.0f);
		void DrawRect(const Vector2& center, const Vector2& size, const Color4& color, float thickness = 1.0f);
		// 回転あり矩形
		void DrawRect(const Vector2& center, const Vector2& size, const Vector2& anchor, float rotationDegrees, const Color4& color, float thickness = 1.0f);
		void DrawRect(const Vector2& center, const Vector2& size, float rotationDegrees, const Color4& color, float thickness = 1.0f);

		// 円
		void DrawCircle(const Vector2& center, float radius, const Color4& color, uint32_t division = 8, float thickness = 1.0f);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// DrawGridで要求されたグリッド描画数
		uint32_t gridDrawCount_ = 0;

		//--------- functions ----------------------------------------------------

		void DrawLineImpl(GraphicsCore& graphicsCore, const ResolvedCameraView* camera, MultiRenderTarget& surface) override;
	};
} // Engine
