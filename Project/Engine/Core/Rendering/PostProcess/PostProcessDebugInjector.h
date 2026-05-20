#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>

// c++
#include <string>

namespace Engine {

	// front
	class GraphicsCore;
	class MultiRenderTarget;
	class PipelineStateCache;
	class PostProcessAssetGenerator;
	class PostProcessExecutor;
	class PostProcessTemporaryTargetPool;
	class RenderAssetLibrary;
	struct RenderFrameRequest;
	struct SceneExecutionContext;

	//============================================================================
	//	PostProcessDebugInjector class
	//	Blit直前へBuiltin PostProcessを一枚だけ挟む確認用の補助クラス。
	//============================================================================
	struct PostProcessDebugInjectSettings {

		bool enabled = true;
		std::string materialName = "Grayscale";
		std::string sourceName = "SceneColorFinal";
		std::string tempName = "PostProcessDebugTemp";
	};

	class PostProcessDebugInjector {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PostProcessDebugInjector() = default;
		~PostProcessDebugInjector() = default;

		// Blit前に有効な場合だけPostProcessDebugTempへ描き、Blit元を差し替える
		bool TryExecuteBeforeBlit(GraphicsCore& graphicsCore, const RenderFrameRequest& request,
			const SceneExecutionContext& context, const BlitPassDesc& pass,
			RenderAssetLibrary& assetLibrary, PipelineStateCache& pipelineCache,
			PostProcessExecutor& executor, PostProcessTemporaryTargetPool& targetPool,
			const PostProcessAssetGenerator& assetGenerator, MultiRenderTarget*& inoutSource);

		//--------- accessor -----------------------------------------------------

		void SetEnabled(bool enabled) { settings_.enabled = enabled; }
		bool IsEnabled() const { return settings_.enabled; }
		void SetSettings(const PostProcessDebugInjectSettings& settings) { settings_ = settings; }
		const PostProcessDebugInjectSettings& GetSettings() const { return settings_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		PostProcessDebugInjectSettings settings_{};
	};
} // Engine
