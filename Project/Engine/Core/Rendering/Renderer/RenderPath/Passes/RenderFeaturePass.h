#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderFeatureTemporalState.h"
#include <Engine/Core/Rendering/DxObject/Buffers/DxRWStructuredBuffer.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

// c++
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace Engine {

	// 固定パスだけが追加パスと異なる実行条件を持つ
	struct RenderFeaturePassOptions {

		bool sceneColorOutput = true;
		bool allowInlineRayTracing = false;
		bool useConfiguredResolution = false;
		bool allowInPlaceReflectionComposite = false;
		bool worldSpaceHistory = false;
	};

	struct RenderFeatureComputeWorkQueue {
		UUID pass;
		AssetID selectionMaterial;
		StructuredRWBuffer<uint32_t>& jobs;
		StructuredRWBuffer<uint32_t>& arguments;
		ID3D12CommandSignature* signature;
		uint32_t selectGroupsX, selectGroupsY;
	};

	struct RenderFeatureSharedOutput {
		const char* shaderResource;
		MultiRenderTarget* surface;
	};

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

		RenderFeaturePass(const RenderPipelineDeps& deps, RenderFeatureAnchor anchor, const RenderFeaturePassOptions& options = {});
		~RenderFeaturePass() override = default;

		void RetainViews(const std::unordered_set<std::string>& activeViews) override;

		void Execute(GraphicsCore& graphicsCore,
			const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		bool HasExecutionSucceeded() const { return executionSucceeded_; }
		// 同じフレームで計算済みのパス出力を別Viewの履歴へ引き継ぐ
		void ExecuteWithSharedOutputs(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context, UUID passID, std::span<const RenderFeatureSharedOutput> outputs,
			const RenderFeatureComputeWorkQueue* queue = nullptr);

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::RenderFeature; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		const RenderPipelineDeps& deps_;
		RenderFeatureAnchor anchor_ = RenderFeatureAnchor::AfterTransparent;
		std::string lastDiagnostic_{};
		RenderFeaturePassOptions options_{};
		bool executionSucceeded_ = false;

		RenderFeatureTemporalState temporalState_{};
		std::vector<float> resolutionScales_;
	};
} // Engine
