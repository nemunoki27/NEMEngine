#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>

namespace Engine {

	//============================================================================
	//	CloneEntityTreesCommand class
	//	複製範囲と選択を一つの履歴で管理する
	//============================================================================
	class CloneEntityTreesCommand final : public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit CloneEntityTreesCommand(std::vector<Entity> targets);
		CloneEntityTreesCommand(std::vector<EditorEntityTreeSnapshot> sources, std::vector<UUID> parents,
			bool retainSourceScene = false);

		// 全階層を準備してからまとめて生成する
		bool Execute(EditorCommandContext& context) override;
		// 全生成物を削除して元の選択へ戻す
		void Undo(EditorCommandContext& context) override;
		// 初回のIDと選択を復元する
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Clone Entity Trees"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::vector<Entity> targets_;
		std::vector<EditorEntityTreeSnapshot> sources_;
		std::vector<UUID> parents_;
		std::vector<UUID> createdRoots_;
		EditorSelectionSnapshot previousSelection_;
		EditorSelectionSnapshot appliedSelection_;
		std::unique_ptr<CompositeEditorCommand> removal_;
		bool retainSourceScene_ = false;

		//--------- functions ----------------------------------------------------

		// 複製元と外部親を変更前に確定する
		bool CaptureSources(ECSWorld& world);
		// WorldとSceneの生存を照合して配置先を決める
		void PrepareDestinations(EditorCommandContext& context, std::vector<EditorEntityTreeSnapshot>& snapshots);
	};
}
