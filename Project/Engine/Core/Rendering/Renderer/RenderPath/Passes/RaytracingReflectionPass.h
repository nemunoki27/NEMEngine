#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	//============================================================================
	//	RaytracingReflectionPass class
	//	LightingPassが書いたSceneColorFinalへレイトレーシング反射を加算するパス
	//	GBufferの法線/位置を入力にし、ベース色はSceneColorFinal自身から読む
	//============================================================================
	class RaytracingReflectionPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit RaytracingReflectionPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~RaytracingReflectionPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return RenderPathPassKind::RaytracingReflection; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;

		mutable AssetID cachedMaterialID_{};
		mutable bool materialSearched_ = false;

		//--------- functions ----------------------------------------------------

		// リフレクションマテリアルのIDを取得する
		AssetID ResolveMaterial() const;
	};
} // Engine

