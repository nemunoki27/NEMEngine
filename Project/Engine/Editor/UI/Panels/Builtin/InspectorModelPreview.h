#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/EditorToolRenderResources.h>
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraController.h>

namespace Engine {

	struct AssetMeta;

	//============================================================================
	//	InspectorModelPreview class
	//	モデル表示用のワールドと描画資源を所有する
	//============================================================================
	class InspectorModelPreview {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		InspectorModelPreview();
		// Meshアセットのプレビューと詳細を描画する
		void DrawMeshAssetInspector(const EditorPanelContext& context, const AssetMeta& meta);
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct ModelAssetPreviewBounds {

			// モデル頂点全体の最小座標
			Vector3 min = Vector3::AnyInit(0.0f);
			// モデル頂点全体の最大座標
			Vector3 max = Vector3::AnyInit(0.0f);
			// プレビューカメラの注視点に使う境界中心
			Vector3 center = Vector3::AnyInit(0.0f);
			// モデル全体を収めるための境界半径
			float radius = 1.0f;
			// 境界計算が成功しているか
			bool valid = false;
		};

		//--------- variables ----------------------------------------------------

		// プレビュー専用の描画資源
		EditorToolRenderResources resources_;

		// Inspectorモデルプレビュー用レンダーターゲットの基本サイズ
		const Vector2I kModelPreviewSize_ = Vector2I(512, 288);
		// Inspectorモデルプレビューの背景色
		const Color4 kModelPreviewColor_ = Color4(0.10f, 0.11f, 0.13f, 1.0f);
		// InspectorモデルプレビューのMRT数
		const uint32_t kModelPreviewColorTargetCount_ = 3;

		// Inspectorで表示中のモデルアセットID
		AssetID modelPreviewAsset_{};
		// Inspectorで最後に処理したアセット選択Revision
		uint64_t modelPreviewSelectionRevision_ = 0;
		// Inspectorモデルプレビュー専用の一時World
		std::unique_ptr<ECSWorld> modelPreviewWorld_;
		// プレビューWorld内のモデルEntity
		Entity modelPreviewEntity_ = Entity::Null();
		// プレビューWorld内のライトEntity
		Entity modelPreviewLightEntity_ = Entity::Null();
		// Inspectorモデルプレビューの境界情報
		ModelAssetPreviewBounds modelPreviewBounds_{};
		// Inspectorモデルプレビュー用の手動カメラ
		std::unique_ptr<SceneViewCameraController> modelPreviewCameraController_;
		// Inspectorモデルプレビュー画像のImGui表示位置
		ImVec2 modelPreviewImagePos_{};

		//--------- functions ----------------------------------------------------

		// Meshアセットプレビュー用の一時Worldを構築する
		void RebuildModelAssetPreviewWorld(const EditorPanelContext& context, const AssetMeta& meta);
		// Meshアセットプレビューをレンダーターゲットへ描画する
		void RenderModelAssetPreview(const EditorToolContext& toolContext, EditorToolRenderTexture& preview);
		// Meshアセットの境界を計算する
		ModelAssetPreviewBounds ComputeModelAssetPreviewBounds(const EditorPanelContext& context, const AssetMeta& meta) const;
		// Meshアセット境界に合わせてInspector側の手動カメラを初期化する
		void ResetModelAssetPreviewCamera();
	};
}
