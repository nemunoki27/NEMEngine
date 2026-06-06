#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

// c++
#include <vector>

namespace Engine {

	// front
	struct RenderItem;

	//============================================================================
	//	InvertedHullOutlinePass class
	//	背面法アウトラインの追加描画パス。OpaqueバケットからOutline対象を抽出し、
	//	SceneFinalの色とSceneMainの深度を組み合わせてHullを描画する
	//============================================================================
	class InvertedHullOutlinePass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit InvertedHullOutlinePass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~InvertedHullOutlinePass() override = default;

		std::string_view GetName() const override { return "InvertedHullOutline"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
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

		const RenderPipelineDeps& deps_;

		// 全outlined entity共通のステンシル予約値
		static constexpr UINT kOutlineStencilReference = 1u;

		//--------- functions ----------------------------------------------------

		// Outline対象アイテムを収集する
		OutlineItemGroups CollectItems(const SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets) const;
	};
} // Engine
