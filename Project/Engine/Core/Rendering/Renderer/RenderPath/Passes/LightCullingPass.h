#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/Pipelines/Bind/PipelineBindingCache.h>
#include <Engine/Core/Rendering/Pipelines/Bind/RegistryAutoBindTable.h>

namespace Engine {

	//============================================================================
	//	LightCullingPass class
	//	DepthPrepass後にTile Light GridをCompute Shaderで生成するパス
	//============================================================================
	class LightCullingPass :
		public IRenderPass {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit LightCullingPass(const RenderPipelineDeps& deps) : deps_(deps) {
			// register(t1, space0) を初期化時に登録しておく
			depthSRVSlot_ = depthSRVCache_.AddSlotByRegister(ShaderBindingKind::SRV, 1, 0);
		}
		~LightCullingPass() override = default;

		std::string_view GetName() const override { return "LightCulling"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		mutable AssetID cachedMaterialID_{};
		mutable bool materialSearched_ = false;

		// 深度SRV: register(t1, space0) のバインドロケーションをキャッシュする
		PipelineBindingCache depthSRVCache_;
		PipelineBindingCache::SlotID depthSRVSlot_ = PipelineBindingCache::kInvalidSlot;
		// バッファレジストリ全エントリとパイプラインスロットの対応キャッシュ
		RegistryAutoBindTable computeAutoBindTable_;

		//--------- functions ----------------------------------------------------

		// ライトカリングマテリアルのIDを取得する
		AssetID ResolveLightCullingMaterial(AssetDatabase& database) const;
	};
} // Engine
