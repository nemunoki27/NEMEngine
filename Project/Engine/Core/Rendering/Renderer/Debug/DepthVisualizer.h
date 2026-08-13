#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

namespace Engine {

	// front
	class GraphicsCore;
	class DepthTexture2D;
	class MultiRenderTarget;
	class RenderTexture2D;

	//============================================================================
	//	DepthVisualizer class
	//	深度テクスチャを画面表示用のグレースケールへ変換するクラス
	//============================================================================
	class DepthVisualizer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		DepthVisualizer();
		~DepthVisualizer() = default;

		// 深度を可視化用レンダーターゲットへ描画する
		RenderTexture2D* Render(GraphicsCore& graphicsCore, DepthTexture2D* depth,
			MultiRenderTarget& output);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		PipelineState pipeline_{};
		PipelineBindingCache bindCache_{};
		PipelineBindingCache::SlotID depthSlot_ = PipelineBindingCache::kInvalidSlot;
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 初回描画時にパイプラインを生成する
		void EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat);
	};
} // Engine
