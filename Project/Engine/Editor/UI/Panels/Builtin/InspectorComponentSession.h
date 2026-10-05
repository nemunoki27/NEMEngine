#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Core/ComponentEditorRegistry.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

namespace Engine {

	class MeshRendererInspectorDrawer;

	//============================================================================
	//	InspectorComponentSession class
	//	Componentの編集Drawerと操作メニューを所有する
	//============================================================================
	class InspectorComponentSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		InspectorComponentSession() = default;
		~InspectorComponentSession() = default;

		// 組込Componentの編集Drawerを登録する
		void Init();
		// 全Drawerのプレビューを終了する
		void EndPreview();
		// 現在の編集対象へプレビューの所有を揃える
		void SyncPreviewOwner(ECSWorld* world, Entity entity);
		// 対象が持つComponentを表示する
		void DrawComponents(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// 選択SubMeshの編集を表示する
		void DrawSubMesh(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// Component追加と削除の操作を表示する
		void DrawComponentToolbar(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// Inspector領域へのScript配置を受け付ける
		void DrawScriptAssetDropTarget(const EditorPanelContext& context, const Entity& entity);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Componentの表示と操作の登録
		ComponentEditorRegistry componentEditorRegistry_{};
		// 追加Componentの検索条件
		TextSearchFilter addComponentSearchFilter_;
		// 削除Componentの検索条件
		TextSearchFilter removeComponentSearchFilter_;
		// 追加Scriptの検索条件
		TextSearchFilter addScriptSearchFilter_;
		// Registry所有のMesh編集Drawerを借用
		MeshRendererInspectorDrawer* meshRendererDrawer_ = nullptr;

		//--------- functions ----------------------------------------------------

		// コンポーネントの追加、削除のポップアップを描画する
		void DrawAddComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// Component削除の確認を表示する
		void DrawRemoveComponentPopup(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// コンポーネント追加ポップアップ右側のC#型一覧を描画する
		void DrawAddScriptEntries(const EditorPanelContext& context, const Entity& entity);
		// 追加削除ポップアップ共通の検索とカテゴリ区切りつきメニュー描画、判定と実行は呼び出し側が渡す
		void DrawComponentPopupEntries(const EditorPanelContext& context, TextSearchFilter& searchFilter,
			const char* searchInputID, const char* emptyText,
			const std::function<bool(const ComponentEditorDescriptor&)>& shouldShow,
			const std::function<bool(const ComponentEditorDescriptor&)>& onSelect,
			const std::function<bool(const ComponentEditorDescriptor&)>& drawCustomEntry = {});
	};
} // Engine
