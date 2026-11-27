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
	//	ReSTIRDIRenderer class
	//	Viewごとにローカルライトのサンプルと再利用履歴を管理する
	//============================================================================
	class ReSTIRDIRenderer {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ReSTIRDIRenderer();
		~ReSTIRDIRenderer();
		ReSTIRDIRenderer(const ReSTIRDIRenderer&) = delete;
		ReSTIRDIRenderer& operator=(const ReSTIRDIRenderer&) = delete;

		// 各画素のライトを選択してLightingへ公開する
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
