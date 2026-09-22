#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureTemporalState.h"
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

// c++
#include <cstdint>
#include <string>
#include <unordered_map>

namespace Engine {

	//============================================================================
	//	RenderFeaturePass class
	//	指定AnchorのComputeとDispatchRaysを共通グラフから実行する
	//============================================================================
	class RenderFeaturePass :
		public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenderFeaturePass(const RenderPipelineDeps& deps, RenderFeatureAnchor anchor) : deps_(deps), anchor_(anchor) {}
		~RenderFeaturePass() override = default;

		void Execute(GraphicsCore& graphicsCore,
			const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::RenderFeature; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		const RenderPipelineDeps& deps_;
		RenderFeatureAnchor anchor_ = RenderFeatureAnchor::AfterTransparent;
		std::string lastDiagnostic_{};

		RenderFeatureTemporalState temporalState_{};
	};
} // Engine
