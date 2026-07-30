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
	//	GBufferの法線/位置/PBRパラメータを入力にし、SceneColorFinalへ反射を合成する
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

		//--------- functions ----------------------------------------------------

		// リフレクションマテリアルのIDを取得する
		AssetID ResolveMaterial() const;
	};
} // Engine

