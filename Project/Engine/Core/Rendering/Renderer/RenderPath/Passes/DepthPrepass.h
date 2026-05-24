#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	DepthPrepass class
	//	Opaqueキューの深度プリパス。LightCulling/Overdraw削減用。
	//============================================================================
	class DepthPrepass :
		public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit DepthPrepass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~DepthPrepass() override = default;

		std::string_view GetName() const override { return "DepthPrepass"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		//--------- functions ----------------------------------------------------

		// 深度プリパス対象アイテムを収集する
		std::vector<const RenderItem*> CollectItems(const SceneExecutionContext& context,
			const RenderPassPhaseBuckets& passBuckets) const;
	};
} // Engine
