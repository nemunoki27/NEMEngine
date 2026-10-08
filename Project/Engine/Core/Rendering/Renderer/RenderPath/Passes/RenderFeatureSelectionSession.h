#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileRuntime.h>
#include <dxgiformat.h>

namespace Engine {

	class GraphicsCore;
	class MultiRenderTarget;
	struct SceneExecutionContext;
	struct RenderPipelineDeps;
	struct RenderPassPhaseBuckets;

	//============================================================================
	//	RenderFeatureSelectionSession class
	//	選択対象の描画と効果の合成を管理する
	//============================================================================
	class RenderFeatureSelectionSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 対象を集めて選択描画先へ描画する
		bool Begin(GraphicsCore& graphicsCore, SceneExecutionContext& context, const RenderPipelineDeps& deps,
			const RenderFeatureProfileRuntime& runtime, const RenderPassPhaseBuckets& passBuckets,
			const RenderFeatureHierarchyItem* selectionGroup, MultiRenderTarget& sceneFinal, DXGI_FORMAT sceneFormat);
		// 効果をSceneへ合成する
		bool Composite(GraphicsCore& graphicsCore, SceneExecutionContext& context, const RenderPipelineDeps& deps,
			const RenderFeatureHierarchyItem* selectionGroup, const std::string& sceneColorAlias,
			const std::string& executionPrimaryAlias, const RenderFeatureOutputReference& primaryReference,
			MultiRenderTarget* primaryTarget, MultiRenderTarget* sceneFinal, const std::string& passName);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		friend class RenderFeaturePass;

		const RenderFeatureHierarchyItem* skippedSelection = nullptr;
		MultiRenderTarget* selectionTarget = nullptr;
		std::string selectionAlias{};
		std::vector<const RenderItem*> selectionItems{};
		std::vector<const RenderItem*> phaseItems{};
	};
}
