#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/RenderTargets/MultiRenderTarget.h>
#include <Engine/Core/Rendering/Renderer/Debug/DepthVisualizer.h>

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

		// プロジェクトからのドラッグ&ドロップ配置を処理する、ドラッグ中プレビューとドロップ確定を扱う
		void HandleAssetDropPlacement(const EditorPanelContext& context, RenderViewKind viewKind,
			const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight, bool imageHovered, const ImVec2& imageSize, bool scenePanel);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// プロジェクトからのドラッグ&ドロップ配置のプレビュー状態
		// ドラッグ中に仮エンティティを作って実際に置きながら見せ、ドロップで確定する
		Entity dropPreviewEntity_ = Entity::Null();
		bool dropPreviewActive_ = false;
		bool dropPreviewIsThreeD_ = false;
		AssetID dropPreviewAsset_{};
		ECSWorld* dropPreviewWorld_ = nullptr;
		// 右クリックで一度キャンセルしたら、そのドラッグが終わるまでプレビューを作らない
		bool dropPreviewCanceled_ = false;

		// 描画中の画像サイズ
		ImVec2 viewSize_{};

		//--------- functions ----------------------------------------------------

		// スナップ有効時に配置座標を現在の座標スナップ設定の間隔へ吸着させる、isThreeDで2D/3Dの設定を切り替える
		void ApplyDropSnap(const EditorPanelContext& context, Vector3& position, bool isThreeD) const;
		// マウス位置から配置先のワールド座標を求める、3Dはカメラ光線と地面、2Dは画面のピクセル空間
		Vector3 ComputeDropPosition(const EditorPanelContext& context, RenderViewKind viewKind, bool isThreeD,
			const ImVec2& imagePos, uint32_t renderWidth, uint32_t renderHeight) const;
		// 配置プレビューの仮エンティティを破棄する
		void DestroyDropPreview();
	};
}
