#pragma once

//============================================================================
//	include
//============================================================================
#include <memory>
#include <string>
#include <unordered_set>

namespace Engine {

	class GraphicsCore;
	struct SceneExecutionContext;
	struct RenderPipelineDeps;

	//============================================================================
	//	InlineShadowRenderer class
	//	View別にレイトレーシング影の遮蔽率と平滑化履歴を所有する
	//============================================================================
	class InlineShadowRenderer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		InlineShadowRenderer();
		~InlineShadowRenderer();
		InlineShadowRenderer(const InlineShadowRenderer&) = delete;
		InlineShadowRenderer& operator=(const InlineShadowRenderer&) = delete;

		// 光源別の遮蔽率を生成してLightingへ公開する
		bool Render(GraphicsCore& graphicsCore, SceneExecutionContext& context, const RenderPipelineDeps& deps);
		// 使用しなくなったViewの履歴を回収する
		void RetainViews(const std::unordered_set<std::string>& viewKeys);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct Implementation;

		//--------- variables ----------------------------------------------------

		std::unique_ptr<Implementation> implementation_;
	};
}
