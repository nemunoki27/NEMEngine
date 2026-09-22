#pragma once

#include "InspectorModelPreview.h"
#include "InspectorMaterialSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>
#include <Engine/Editor/UI/Inspectors/Core/ComponentEditorRegistry.h>
#include <Engine/Editor/UI/Inspectors/Core/AssetInspectorRegistry.h>
#include <Engine/Core/Rendering/Assets/MaterialAsset.h>
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraController.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <array>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

namespace Engine {

	// front
	struct AssetMeta;
	class MeshRendererInspectorDrawer;

	//============================================================================
	//	InspectorPanel class
	//	インスペクターパネル
	//============================================================================
	class InspectorPanel :
		public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		InspectorPanel(const std::string& instanceID = "inspector.primary", bool primaryInstance = true);
		~InspectorPanel() = default;

		void Draw(const EditorPanelContext& context) override;
		nlohmann::json SaveLayoutState() const override;
		void LoadLayoutState(const nlohmann::json& state) override;
		nlohmann::json MakeDuplicateState(const EditorPanelContext& context) const override;

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
		bool CanDuplicate(const EditorPanelContext& context) const override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 名前編集用の一時バッファ
		std::string nameEditBuffer_;
		UUID editingNameEntityStableUUID_{};
		// 複製時に固定したエンティティ
		UUID lockedEntityUUID_{};

		// Materialの編集セッション
		InspectorMaterialSession materialSession_;

		// コンポーネントの追加メニューと描画登録
		ComponentEditorRegistry componentEditorRegistry_{};
		TextSearchFilter addComponentSearchFilter_;
		TextSearchFilter removeComponentSearchFilter_;
		TextSearchFilter addScriptSearchFilter_;
		// アセット種別ごとのInspector表示登録
		AssetInspectorRegistry assetInspectorRegistry_{};

		// メッシュインスペクター
		MeshRendererInspectorDrawer* meshRendererDrawer_ = nullptr;

		// オーバーライドポップアップの各差分の選択、0=そのまま 1=Apply 2=Revert
		std::unordered_map<std::string, int> overrideChoices_;

		// モデルプレビューの所有
		InspectorModelPreview modelPreview_;
		// 表示文字サイズ
		const float fontScale_ = 0.92f;
		//--------- functions ----------------------------------------------------

		// エンティティのヘッダー部分を描画する
		void DrawEntityHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// 選択中のアセットのインスペクターを描画する
		void DrawSelectedAssetInspector(const EditorPanelContext& context);
		// 名前の同期
		void SyncNameBufferIfNeeded(ECSWorld& world, const Entity& entity);
		// コンポーネントのツールバーを描画する
		void DrawComponentToolbar(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// コンポーネントの追加、削除のポップアップを描画する
		void DrawAddComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// Component削除の確認を表示する
		void DrawRemoveComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// コンポーネント追加ポップアップ右側のC#型一覧を描画する
		void DrawAddScriptEntries(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// Inspector全体でProjectPanelからのC#スクリプトドロップを受け取る
		void DrawScriptAssetDropTarget(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// 追加削除ポップアップ共通の検索とカテゴリ区切りつきメニュー描画、判定と実行は呼び出し側が渡す
		void DrawComponentPopupEntries(const EditorPanelContext& context, TextSearchFilter& searchFilter,
			const char* searchInputID, const char* emptyText,
			const std::function<bool(const ComponentEditorDescriptor&)>& shouldShow,
			const std::function<bool(const ComponentEditorDescriptor&)>& onSelect,
			const std::function<bool(const ComponentEditorDescriptor&)>& drawCustomEntry = {});
		// サブメッシュが選択されているときのヘッダーを描画する
		void DrawSelectedSubMeshHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// プレファブインスタンスのオーバーライド表示UI、水色強調トークンの構築とオーバーライドポップアップを描画する
		void DrawPrefabOverrideUI(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// スキンメッシュのジョイントが選択されているときのインスペクターを描画する
		void DrawJointInspector(const EditorPanelContext& context);
	};
} // Engine
