#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/FixedForwardPlusRenderPath.h>

namespace Engine {

	//============================================================================
	//	OpaqueRenderPass class
	//	Opaqueキューを SceneMain に描画するパス
	//============================================================================
	class OpaqueRenderPass :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================
		explicit OpaqueRenderPass(const RenderPipelineDeps& deps) : deps_(deps) {}
		~OpaqueRenderPass() override = default;

		std::string_view GetName() const override { return "Opaque"; }
		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================
		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
	};
} // Engine
