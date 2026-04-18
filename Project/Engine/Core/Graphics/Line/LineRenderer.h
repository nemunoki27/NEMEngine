#pragma once
#if defined(_DEBUG) || defined(_DEVELOPBUILD)

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Line/Dimensions/LineRenderer2D.h>
#include <Engine/Core/Graphics/Line/Dimensions/LineRenderer3D.h>

namespace Engine {

	// front
	class GraphicsCore;

	//============================================================================
	//	LineRenderer class
	//	ライン描画を行うクラス
	//============================================================================
	class LineRenderer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 初期化
		void Init(GraphicsCore& graphicsCore);

		// フレーム開始処理
		void BeginFrame();

		// 描画
		void RenderSceneView(GraphicsCore& graphicsCore, const ResolvedRenderView& view, MultiRenderTarget& surface);

		//--------- accessor -----------------------------------------------------

		// 各次元のライン描画クラスのアクセサ
		LineRenderer2D* Get2D() const { return renderer2D_.get(); }
		LineRenderer3D* Get3D() const { return renderer3D_.get(); }

		// singleton
		static LineRenderer* GetInstance();
		static void Finalize();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		static LineRenderer* instance_;

		// 各次元のライン描画クラス
		std::unique_ptr<LineRenderer2D> renderer2D_{};
		std::unique_ptr<LineRenderer3D> renderer3D_{};

		//--------- functions ----------------------------------------------------

		LineRenderer() = default;
		~LineRenderer() = default;
		LineRenderer(const LineRenderer&) = delete;
		LineRenderer& operator=(const LineRenderer&) = delete;
	};
} // Engine
#endif