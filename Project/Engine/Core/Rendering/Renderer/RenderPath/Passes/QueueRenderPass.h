#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/RenderPath/IRenderPass.h>
#include <Engine/Core/Rendering/Renderer/RenderPath/DeferredRenderPath.h>

namespace Engine {

	//============================================================================
	//	QueueRenderPass class
	//	描画キューを指定Surfaceへ描画する共通パス
	//============================================================================
	class QueueRenderPass final :
		public IRenderPass {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		enum class Target : uint8_t {
			SceneMain,
			SceneFinal,
			DefaultSurface,
		};

		struct Desc {

			RenderPathPassKind kind = RenderPathPassKind::Opaque;
			RenderPhase phase = RenderPhase::Opaque;
			Target target = Target::SceneMain;
			MaterialPassKind materialPass = MaterialPassKind::Draw;
			bool usePhaseExecution = true;
			bool forceVertexMeshVariant = false;
			bool reuseSceneDepth = false;
		};

		QueueRenderPass(const RenderPipelineDeps& deps, const Desc& desc) :
			deps_(deps), desc_(desc) {}
		~QueueRenderPass() override = default;

		void Execute(GraphicsCore& graphicsCore, const RenderPassPhaseBuckets& passBuckets,
			SceneExecutionContext& context) override;

		//--------- accessor -----------------------------------------------------

		RenderPathPassKind GetKind() const override { return desc_.kind; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		const RenderPipelineDeps& deps_;
		Desc desc_{};
	};
} // Engine
