#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Rendering/Core/RenderingFeatureTypes.h>
#include <dxgiformat.h>

namespace Engine {

	class GraphicsCore;
	class MultiRenderTarget;
	class PostProcessTemporaryTargetPool;
	class RenderFeatureTemporalState;
	struct SceneExecutionContext;

	//============================================================================
	//	RenderFeaturePassTargets class
	//	Passの出力と履歴入力を解決する
	//============================================================================
	class RenderFeaturePassTargets {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 出力を確保して履歴条件を更新する
		bool Resolve(GraphicsCore& graphicsCore, SceneExecutionContext& context, PostProcessTemporaryTargetPool& pool,
			RenderFeatureTemporalState& temporalState, const RenderFeaturePassSettings& pass, MultiRenderTarget& sceneFinal,
			DXGI_FORMAT sceneFormat, float outputScale, bool raytracingChain, const GraphicsRuntimeFeatures& runtimeFeatures,
			uint64_t runtimeGeneration);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		friend class RenderFeaturePass;

		RenderFeatureOutputSettings primaryOutput{};
		RenderFeatureOutputReference primaryReference{};
		std::unordered_map<std::string, MultiRenderTarget*> outputTargets{};
		std::unordered_map<std::string, std::string> historyInputs{};
		std::vector<std::string> writtenHistoryKeys{};
		std::vector<RenderFeatureOutputSettings> defaultOutputs{};
	};
}
