#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>
#include <Engine/Editor/Inspector/Interface/IInspectorComponentDrawer.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <array>
#include <memory>
#include <vector>

namespace Engine {

	// front
	class MeshRendererInspectorDrawer;

	//============================================================================
	//	InspectorPanel class
	//	インスペクターパネル
	//============================================================================
	class InspectorPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		InspectorPanel();
		~InspectorPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 名前編集用の一時バッファ
		std::string nameEditBuffer_;
		UUID editingNameEntityStableUUID_{};

		// コンポーネントの編集
		std::vector<std::unique_ptr<IInspectorComponentDrawer>> componentDrawers_{};

		// メッシュインスペクター
		MeshRendererInspectorDrawer* meshRendererDrawer_ = nullptr;

		// 表示文字サイズ
		const float fontScale_ = 0.72f;

		//--------- functions ----------------------------------------------------

		// エンティティのヘッダー部分を描画する
		void DrawEntityHeader(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
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