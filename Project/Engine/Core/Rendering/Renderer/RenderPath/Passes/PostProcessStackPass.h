#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>

namespace Engine {

	//============================================================================
	//	PostProcessStackPass class
	//	SceneFinalにポストプロセスエフェクトを適用するパス
	//============================================================================
	class PostProcessStackPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit PostProcessStackPass(const RenderPipelineDeps& deps) : deps_(deps) {
			previewToneMapSrcColorSlot_ = previewToneMapSRVCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
		}
		~PostProcessStackPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets, SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::PostProcessStack; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		// プレビューをGameViewと同じトーンマップ後の見た目で出すための全画面blit用SRVキャッシュ
		PipelineBindingCache previewToneMapSRVCache_{};
		PipelineBindingCache::SlotID previewToneMapSrcColorSlot_ = PipelineBindingCache::kInvalidSlot;
	};
} // Engine

