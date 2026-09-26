#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Pipelines/PipelineState.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Renderer/Backends/Common/ViewConstantBuffer.h>
#include <Engine/Core/Rendering/Core/GraphicsFrameContext.h>

// c++
#include <array>

namespace Engine {

	// front
	class GraphicsCore;
	class DepthTexture2D;
	class MultiRenderTarget;
	class RenderTexture2D;
	struct ResolvedCameraView;

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
		RenderTexture2D* Render(GraphicsCore& graphicsCore,
			const ResolvedCameraView& camera, DepthTexture2D* depth,
			MultiRenderTarget& output);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------
		struct DepthVisualizeConstants {

			float projectionA = 1.0f;
			float projectionB = 0.0f;
			float nearClip = 0.1f;
			float farClip = 1000.0f;

			uint32_t perspective = 1;
			uint32_t _pad[3] = {};
		};

		std::unique_ptr<PipelineState> pipeline_{};
		PipelineBindingCache bindCache_{};
		ViewConstantBuffer<DepthVisualizeConstants> constants_{};
		PipelineBindingCache::SlotID constantsSlot_ =
			PipelineBindingCache::kInvalidSlot;
		PipelineBindingCache::SlotID depthSlot_ = PipelineBindingCache::kInvalidSlot;
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 初回描画時にパイプラインを生成する
		void EnsurePipeline(GraphicsCore& graphicsCore, DXGI_FORMAT colorFormat);
	};
} // Engine
