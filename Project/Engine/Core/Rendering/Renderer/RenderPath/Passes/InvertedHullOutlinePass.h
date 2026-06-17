#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

// c++
#include <vector>

namespace Engine {

	// front
	struct RenderItem;

	//============================================================================
	//	InvertedHullOutlinePass class
	//	背面法アウトラインの追加描画パス
	//============================================================================
	class InvertedHullOutlinePass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit InvertedHullOutlinePass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~InvertedHullOutlinePass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::InvertedHullOutline; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- structure ----------------------------------------------------

		// useStencilの有無で描画対象を二群に分ける
		struct OutlineItemGroups {

			std::vector<const RenderItem*> regularItems;
			std::vector<const RenderItem*> stencilItems;
		};

		//--------- variables ----------------------------------------------------

		// 全アウトラインエンティティの共通のステンシル予約値
		static constexpr UINT kOutlineStencilReference = 1;

		const RenderPipelineDeps& deps_;

		//--------- functions ----------------------------------------------------

		// アウトライン対象アイテムを収集する
		OutlineItemGroups CollectItems(const SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets) const;
	};
} // Engine

