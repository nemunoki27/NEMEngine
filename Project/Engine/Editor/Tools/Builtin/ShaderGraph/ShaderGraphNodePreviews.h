#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAppearance.h"
#include <Engine/Editor/Tools/Core/EditorToolRenderResources.h>
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace Engine {

	//============================================================================
	//	ShaderGraphNodePreviews class
	//	ノードの簡易評価とプレビュー描画資源を所有する
	//============================================================================
	class ShaderGraphNodePreviews {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphNodePreviews();
		~ShaderGraphNodePreviews();
		// 描画状態を初期化する
		void Init();
		// ノードのプレビューを更新する
		void UpdateNodePreviews(const EditorToolContext& context, const ShaderGraphAsset& graph,
			const ShaderGraphAppearanceSetting& appearance, std::string& status);
		// ノード内にプレビューを表示する
		void DrawNodePreview(ShaderGraphNode& node, float nodeWidth, const ShaderGraphAppearanceSetting& appearance);
		// 評価結果を失効させる
		void InvalidateNodePreviews();
		// 作成した画像を破棄する
		void ClearNodePreviews();
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct PreviewState;

		//--------- variables ----------------------------------------------------

		EditorToolRenderResources resources_;
		std::unique_ptr<PreviewState> previewState_;

		//--------- functions ----------------------------------------------------

		// 描画中の資源を使用して評価する
		void UpdateResources(const EditorToolContext& context, const ShaderGraphAsset& graph,
			const ShaderGraphAppearanceSetting& appearance, std::string& status);
	};
}
