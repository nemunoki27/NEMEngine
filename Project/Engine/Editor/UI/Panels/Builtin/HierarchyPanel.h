#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

// c++
#include <cstdint>
#include <vector>
#include <unordered_map>

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
		//============================================================================
		//	public Methods
		//============================================================================

		explicit HierarchyPanel(TextureUploadService& textureUploadService);
		~HierarchyPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// アクティブ表示アイコンを読み込む
		void RequestActiveIconTextures();
		// アクティブ状態の表示/切り替えボタンを描画する
		void DrawActiveToggleIcon(const EditorPanelContext& context, ECSWorld& world,
			const Entity& entity, bool activeSelf, bool& leftClicked, bool& rightClicked);
		// エンティティノードを描画する
		void DrawEntityNode(const EditorPanelContext& context, ECSWorld& world, const Entity& entity, bool forceVisible);
		// 同じ階層内の表示順を変えるためのドロップ目標を描画する
		void DrawSiblingDropTarget(const EditorPanelContext& context, ECSWorld& world,
			const Entity& anchorEntity, bool insertAfter);
		void DrawSubMeshNodes(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// スキンメッシュのジョイント階層を描画する、サブメッシュと同様にエンティティの下に出す
		void DrawSkinnedMeshNodes(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);
		// ジョイントノードを再帰的に描画する、ジョイント直下に親子付けエンティティも出す
		void DrawJointNode(const EditorPanelContext& context, ECSWorld& world, const Entity& skinnedEntity,
			int32_t jointIndex, const std::unordered_map<int32_t, std::vector<Entity>>& attachedByJoint);
		// ヒエラルキーパネルの背景を右クリックしたときのコンテキストメニューを描画する
		void DrawBackgroundContextMenu(const EditorPanelContext& context);
		// ルートエンティティでないエンティティをドロップしてルートエンティティにするためのドロップ目標を描画する
		void DrawRootDropTarget(const EditorPanelContext& context, ECSWorld& world);

		// ルートエンティティかどうか
		bool IsRootEntity(ECSWorld& world, const Entity& entity) const;
		// 親エンティティを取得する
		Entity GetParentEntity(ECSWorld& world, const Entity& entity) const;
		// 親子関係を変更できるか
		bool CanReparent(const EditorPanelContext& context, ECSWorld& world,
			const Entity& child, const Entity& newParent) const;
		// 表示順を変更できるか
		bool CanReorder(const EditorPanelContext& context, ECSWorld& world,
			const Entity& child, const Entity& anchor) const;
		// ドロップされたペイロードからエンティティを取得する
		Entity ResolveDraggedEntity(ECSWorld& world, const ImGuiPayload* payload) const;
		// 検索条件に一致するエンティティか
		bool EntityMatchesSearch(ECSWorld& world, const Entity& entity) const;
		// 自分または子孫に検索条件へ一致するエンティティがあるか
		bool ShouldDrawEntityNode(ECSWorld& world, const Entity& entity) const;

		//--------- variables ----------------------------------------------------

		TextureUploadService* textureUploadService_ = nullptr;
		bool activeIconRequested_ = false;
		uint32_t visibleEntityRowIndex_ = 0;
		TextSearchFilter searchFilter_;
	};
} // Engine

