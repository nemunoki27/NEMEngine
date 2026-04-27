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

		//---------- drawers -----------------------------------------------------

		// アンカー付き/無し矩形
		void DrawRect(const Vector2& center, const Vector2& size, const Vector2& anchor, const Color4& color, float thickness = 1.0f);
		void DrawRect(const Vector2& center, const Vector2& size, const Color4& color, float thickness = 1.0f);

		// 円
		void DrawCircle(const Vector2& center, float radius, const Color4& color, uint32_t division = 8, float thickness = 1.0f);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawLineImpl(GraphicsCore& graphicsCore, const ResolvedCameraView* camera, MultiRenderTarget& surface) override;
	};
} // Engine