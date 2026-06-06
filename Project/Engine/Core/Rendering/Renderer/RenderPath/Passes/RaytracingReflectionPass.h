#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	//============================================================================
	//	RaytracingReflectionPass class
	//	SceneMain → SceneFinal へレイトレーシング反射を合成するパス
	//============================================================================
	class RaytracingReflectionPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit RaytracingReflectionPass(const RenderPipelineDeps& deps) : deps_(deps) {
			srcColorSlot_ = blitSRVCache_.AddSlotByRegister(ShaderBindingKind::SRV, 0, 0);
		}
		~RaytracingReflectionPass() override = default;

		std::string_view GetName() const override { return "RaytracingReflection"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================
		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		// フォールバック用フルスクリーンブリットのSRVスロット（ソースカラー t0）のキャッシュ
		PipelineBindingCache blitSRVCache_{};
		PipelineBindingCache::SlotID srcColorSlot_ = PipelineBindingCache::kInvalidSlot;

		mutable AssetID cachedMaterialID_{};
		mutable bool materialSearched_ = false;

		//--------- functions ----------------------------------------------------

		// リフレクションマテリアルのIDを取得する
		AssetID ResolveMaterial(AssetDatabase& database) const;
	};
} // Engine
