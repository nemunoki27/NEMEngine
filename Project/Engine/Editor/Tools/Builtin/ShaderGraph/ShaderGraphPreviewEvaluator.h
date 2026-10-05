#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolRenderResources.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

// c++
#include <memory>
#include <string>

namespace Engine {

	//============================================================================
	//	ShaderGraphPreviewEvaluator class
	//	ノードのGPU評価と描画資源を所有する
	//============================================================================
	class ShaderGraphPreviewEvaluator {
	  public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphPreviewEvaluator();
		~ShaderGraphPreviewEvaluator();

		// 評価状態を初期化する
		void Init();
		// ノードの評価結果を描画先へ更新する
		void Update(const EditorToolContext& context, const ShaderGraphAsset& graph, int32_t textureSize, float time,
			float deltaTime, std::string& status);
		// 評価済みの内容を失効させる
		void InvalidateNodePreviews();
		// 評価用の画像を回収へ渡す
		void ClearNodePreviews();

		//--------- accessor -----------------------------------------------------

		const EditorToolRenderTexture* FindTexture(const std::string& name) const;

	  private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct PreviewState;

		//--------- variables ----------------------------------------------------

		// 描画先と評価用のPipelineと定数Buffer
		EditorToolRenderResources resources_;
		std::unique_ptr<PreviewState> previewState_;

		//--------- functions ----------------------------------------------------

		// フレーム中の描画参照でGPU評価を実行する
		void UpdateResources(const EditorToolContext& context, const ShaderGraphAsset& graph, int32_t textureSize, float time,
			float deltaTime, std::string& status);
	};
} // Engine
