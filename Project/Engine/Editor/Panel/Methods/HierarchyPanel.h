#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

namespace Engine {

	// front
	class TextureUploadService;

	//============================================================================
	//	HierarchyPanel class
	//	ヒエラルキーパネル
	//============================================================================
	class HierarchyPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit HierarchyPanel(TextureUploadService& textureUploadService);
		~HierarchyPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// アクティブ表示アイコンを読み込む
		void RequestActiveIconTextures();
		// アクティブ状態の表示/切り替えボタンを描画する
		void DrawActiveToggleIcon(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool activeSelf, bool& leftClicked, bool& rightClicked);
		// エンティティノードを描画する
		void DrawEntityNode(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		void DrawSubMeshNodes(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// ヒエラルキーパネルの背景を右クリックしたときのコンテキストメニューを描画する
		void DrawBackgroundContextMenu(const EditorPanelContext& context);
		// ルートエンティティでないエンティティをドロップしてルートエンティティにするためのドロップ目標を描画する
		void DrawRootDropTarget(const EditorPanelContext& context, ECSWorld& world);

		// ルートエンティティかどうか
		bool IsRootEntity(ECSWorld& world, const Entity& entity) const;
		// 親子関係を変更できるか
		bool CanReparent(ECSWorld& world, const Entity& child, const Entity& newParent) const;
		// ドロップされたペイロードからエンティティを取得する
		Entity ResolveDraggedEntity(ECSWorld& world, const ImGuiPayload* payload) const;
		// エンティティの表示名を取得する
		std::string GetEntityDisplayName(ECSWorld& world, const Entity& entity) const;

		//--------- variables ----------------------------------------------------

		TextureUploadService* textureUploadService_ = nullptr;
		bool activeIconRequested_ = false;
	};
} // Engine
