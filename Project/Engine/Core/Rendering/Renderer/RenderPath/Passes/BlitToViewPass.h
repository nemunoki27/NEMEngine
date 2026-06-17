#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

namespace Engine {

	//============================================================================
	//	BlitToViewPass class
	//	HDR SceneFinalをToneMapしてデフォルトサーフェスへ出力する最終表示パス
	//============================================================================
	class BlitToViewPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit BlitToViewPass(const RenderPipelineDeps& deps) : deps_(deps) {
			srcColorSlot_ = blitSRVCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
		}
		~BlitToViewPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::BlitToView; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		// フルスクリーンブリット用SRVスロットでソースカラーt0のキャッシュ
		PipelineBindingCache blitSRVCache_{};
		PipelineBindingCache::SlotID srcColorSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine

