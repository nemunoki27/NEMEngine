#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	IInspectorComponentDrawer class
	//	Inspector内で各コンポーネントの描画責務を分離するための基底
	//============================================================================
	class IInspectorComponentDrawer {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		IInspectorComponentDrawer() = default;
		virtual ~IInspectorComponentDrawer() = default;

		// 描画
		virtual void Draw(const EditorPanelContext& context, ECSWorld& world, const Entity& entity) = 0;
		// 選択先変更時に前の対象へのプレビューを終了する
		virtual void SyncPreviewOwner([[maybe_unused]] ECSWorld* world, [[maybe_unused]] const Entity& entity) {}
		// Panelの非表示と破棄前にプレビューを終了する
		virtual void EndPreview() {}

		// 編集を確定してから共通の削除処理を行う
		bool RemoveComponent(const EditorPanelContext& context, ECSWorld& world, const Entity& entity);

		//--------- accessor -----------------------------------------------------

		// 対象エンティティに対して描画できるか
		virtual bool CanDraw(ECSWorld& world, const Entity& entity) const = 0;
		// コンポーネントの編集元を登録する
		void SetEditorSourceFile(const std::string& sourceFile) { editorSourceFile_ = sourceFile; }
		void SetComponentTypeName(const std::string& name) { componentTypeName_ = name; }
		const std::string& GetComponentTypeName() const { return componentTypeName_; }
		void SetDisplayName(const std::string& name) { displayName_ = name; }
		const std::string& GetDisplayName() const { return displayName_; }
		// Engine/Editor基準の編集元を取得する
		const std::string& GetEditorSourceFile() const { return editorSourceFile_; }
	protected:
		//========================================================================
		//	protected Methods
		//========================================================================

		// 削除前に未確定の編集を履歴へ保存する
		virtual void CommitPendingEdits([[maybe_unused]] const EditorPanelContext& context,
			[[maybe_unused]] ECSWorld& world, [[maybe_unused]] const Entity& entity) {}
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// コンポーネントのInspector描画を実装する相対パス
		std::string editorSourceFile_;
		std::string displayName_;
		std::string componentTypeName_;
	};
} // Engine
