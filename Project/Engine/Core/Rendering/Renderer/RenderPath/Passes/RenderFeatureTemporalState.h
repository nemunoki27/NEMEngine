#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <string>
#include <unordered_map>

namespace Engine {

	class GraphicsCore;
	class MultiRenderTarget;
	//============================================================================
	//	RenderFeatureTemporalState class
	//	View別の履歴と解像度状態を保持する
	//============================================================================
	class RenderFeatureTemporalState {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 計測結果から解像度係数を更新する
		float UpdateResolution(const RenderFeaturePassSettings& pass, const std::string& stateKey, const std::string& profileName);
		// 条件が変わった履歴を初期化する
		void PrepareHistory(GraphicsCore& graphicsCore, MultiRenderTarget* previous, const std::string& historyKey,
			uint32_t width, uint32_t height, uint64_t runtimeGeneration, uint64_t materialGeneration);
		void MarkWritten(const std::string& historyKey) { historyStates_[historyKey].valid = true; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		struct HistoryState {

			uint32_t width = 0;
			uint32_t height = 0;
			uint64_t runtimeGeneration = 0;
			uint64_t raytracingMaterialGeneration = 0;
			bool valid = false;
		};

		struct AdaptiveResolutionState {

			float scale = 1.0f;
			float filteredGPUMs = 0.0f;
			uint64_t lastAdjustmentFrame = 0;
			uint32_t overBudgetSamples = 0;
			uint32_t underBudgetSamples = 0;
		};

		std::unordered_map<std::string, HistoryState> historyStates_{};
		std::unordered_map<std::string, AdaptiveResolutionState>
			adaptiveResolutionStates_{};
	};
}
