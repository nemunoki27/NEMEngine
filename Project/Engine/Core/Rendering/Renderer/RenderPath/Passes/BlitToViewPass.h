#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

namespace Engine {

	//============================================================================
	//	BlitToViewPass class
	//	HDR SceneFinal をToneMapしてデフォルトサーフェスへ出力する最終表示パス
	//============================================================================
	class BlitToViewPass :
		public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit BlitToViewPass(const RenderPipelineDeps& deps) : deps_(deps) {
			srcColorSlot_ = blitSRVCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
		}
		~BlitToViewPass() override = default;

		std::string_view GetName() const override { return "BlitToView"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		// フルスクリーンブリット用SRVスロット（ソースカラー t0）のキャッシュ
		PipelineBindingCache blitSRVCache_{};
		PipelineBindingCache::SlotID srcColorSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine
