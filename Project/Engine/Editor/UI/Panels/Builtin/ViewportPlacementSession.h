#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Editor/Utility/EditorEntityPreview.h>

namespace Engine {

	struct GizmoViewportRect;

	//============================================================================
	//	ViewportPlacementSession class
	//	配置プレビューの仮エンティティを管理する
	//============================================================================
	class ViewportPlacementSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Assetの配置プレビューと確定を処理する
		void HandleAssetDropPlacement(const EditorPanelContext& context, RenderViewKind viewKind, const ImVec2& imagePos,
			uint32_t renderWidth, uint32_t renderHeight, bool imageHovered, const ImVec2& imageSize, bool scenePanel);
		// 仮Entityを破棄して配置を終了する
		void EndPreview();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 仮Entityと開始Worldの所有
		EditorEntityPreview preview_;
		bool dropPreviewIsThreeD_ = false;
		AssetID dropPreviewAsset_{};
		// 同じドラッグでの再生成を止める
		bool dropPreviewCanceled_ = false;

		// 描画中の画像サイズ
		ImVec2 viewSize_{};

		//--------- functions ----------------------------------------------------

		// 描画空間に対応する配置スナップを適用する
		void ApplyDropSnap(const EditorPanelContext& context, Vector3& position, bool isThreeD) const;
		// マウス位置から配置先の座標を求める
		Vector3 ComputeDropPosition(const EditorPanelContext& context, RenderViewKind viewKind, bool isThreeD,
			const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight) const;
		// 配置プレビューの仮エンティティを破棄する
		void DestroyDropPreview();
	};
}
