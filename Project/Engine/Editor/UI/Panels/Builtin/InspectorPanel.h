#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/UI/Inspectors/Core/IInspectorComponentDrawer.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Core/Rendering/Renderer/Views/SceneViewCameraController.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <array>
#include <memory>
#include <vector>

namespace Engine {

	// front
	struct AssetMeta;
	class MeshRendererInspectorDrawer;

	//============================================================================
	//	InspectorPanel class
	//	インスペクターパネル
	//============================================================================
	class InspectorPanel :
		public IEditorPanel,
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		InspectorPanel();
		~InspectorPanel() = default;

		void Draw(const EditorPanelContext& context) override;
		void DrawEditorTool(const EditorToolContext& context) override;

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// モデルアセットプレビュー用の内部EditorTool定義
		ToolDescriptor descriptor_{
			.id = "engine.inspector_panel",
			.name = "InspectorPanel",
			.category = "Editor",
			.description = "Internal InspectorPanel model asset preview renderer.",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = -99,
		};

		// 名前編集用の一時バッファ
		std::string nameEditBuffer_;
		UUID editingNameEntityStableUUID_{};

		// Materialアセット編集用の一時データ
		AssetID editingMaterialAsset_{};
		MaterialAsset materialDraft_{};
		bool materialDraftValid_ = false;

		// コンポーネントの編集
		std::vector<std::unique_ptr<IInspectorComponentDrawer>> componentDrawers_{};

		// メッシュインスペクター
		MeshRendererInspectorDrawer* meshRendererDrawer_ = nullptr;

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

		// 表示文字サイズ
		const float fontScale_ = 0.72f;
		// Inspectorモデルプレビュー用レンダーターゲットの基本サイズ
		const Vector2I kModelPreviewSize_ = Vector2I(512, 288);
		// Inspectorモデルプレビューの背景色
		const Color4 kModelPreviewColor_ = Color4(0.10f, 0.11f, 0.13f, 1.0f);
		// InspectorモデルプレビューのMRT数
		const uint32_t kModelPreviewColorTargetCount_ = 3;

		//--------- functions ----------------------------------------------------

		// エンティティのヘッダー部分を描画する
		void DrawEntityHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// 選択中のアセットのインスペクターを描画する
		void DrawSelectedAssetInspector(const EditorPanelContext& context);
		// Meshアセットのプレビューと詳細を描画する
		void DrawMeshAssetInspector(const EditorPanelContext& context, const AssetMeta& meta);
		// Meshアセットプレビュー用の一時Worldを構築する
		void RebuildModelAssetPreviewWorld(const EditorPanelContext& context, const AssetMeta& meta);
		// Meshアセットプレビューをレンダーターゲットへ描画する
		void RenderModelAssetPreview(const EditorToolContext& toolContext, EditorToolRenderTexture& preview);
		// Meshアセットの境界を計算する
		ModelAssetPreviewBounds ComputeModelAssetPreviewBounds(const EditorPanelContext& context, const AssetMeta& meta) const;
		// Meshアセット境界に合わせてInspector側の手動カメラを初期化する
		void ResetModelAssetPreviewCamera();
		// Materialアセットのインスペクターを描画する
		void DrawMaterialAssetInspector(const EditorPanelContext& context, const AssetMeta& meta);
		// Materialアセットの編集用データを読み込む
		bool LoadMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta);
		// Materialアセットの編集用データを保存する
		void SaveMaterialDraft(const EditorPanelContext& context, const AssetMeta& meta);
		// 名前の同期
		void SyncNameBufferIfNeeded(ECSWorld& world, const Entity& entity);
		// コンポーネントのツールバーを描画する
		void DrawComponentToolbar(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// コンポーネントの追加、削除のポップアップを描画する
		void DrawAddComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		void DrawRemoveComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// サブメッシュが選択されているときのヘッダーを描画する
		void DrawSelectedSubMeshHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
	};
} // Engine
