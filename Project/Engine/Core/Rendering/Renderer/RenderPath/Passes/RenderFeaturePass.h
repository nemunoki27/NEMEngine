#pragma once

//============================================================================
//	include
//============================================================================
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

		RenderFeaturePass(const RenderPipelineDeps& deps,
			RenderFeatureAnchor anchor) :
			deps_(deps), anchor_(anchor) {
		}
		~RenderFeaturePass() override = default;

		void Execute(GraphicsCore& graphicsCore,
			const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		RenderPathPassKind GetKind() const override {

			return RenderPathPassKind::RenderFeature;
		}

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		const RenderPipelineDeps& deps_;
		RenderFeatureAnchor anchor_ = RenderFeatureAnchor::AfterTransparent;
		std::string lastDiagnostic_{};

		struct HistoryState {

			uint32_t width = 0;
			uint32_t height = 0;
			uint64_t runtimeGeneration = 0;
			uint64_t raytracingMaterialGeneration = 0;
			bool valid = false;
		};
		struct AdaptiveResolutionState {

			float scale = 1.0f;
			float filteredGpuMs = 0.0f;
			uint64_t lastAdjustmentFrame = 0;
			uint32_t overBudgetSamples = 0;
			uint32_t underBudgetSamples = 0;
		};

		std::unordered_map<std::string, HistoryState> historyStates_{};
		std::unordered_map<std::string, AdaptiveResolutionState>
			adaptiveResolutionStates_{};
	};
} // Engine
