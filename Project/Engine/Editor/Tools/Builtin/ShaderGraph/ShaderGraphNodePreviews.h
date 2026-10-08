#pragma once

//============================================================================
//	include
//============================================================================
#include "ShaderGraphAppearance.h"
#include "ShaderGraphPreviewEvaluator.h"
#include <Engine/Core/Rendering/ShaderGraph/ShaderGraphAsset.h>

namespace Engine {

	//============================================================================
	//	ShaderGraphNodePreviews class
	//	ノードのプレビュー画像と背景を表示する
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

		//--------- variables ----------------------------------------------------

		// 評価結果と描画資源の所有
		ShaderGraphPreviewEvaluator evaluator_;
	};
} // Engine
