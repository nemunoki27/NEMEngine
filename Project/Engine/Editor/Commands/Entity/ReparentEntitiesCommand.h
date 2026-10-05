#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Editor/Commands/Core/CompositeEditorCommand.h>

// c++
#include <memory>
#include <vector>

namespace Engine {

	//============================================================================
	//	ReparentEntitiesCommand class
	//	複数Entityの親変更を1件のUndoへまとめる
	//============================================================================
	class ReparentEntitiesCommand : public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ReparentEntitiesCommand(std::vector<Entity> targetEntities, UUID newParentStableUUID = UUID{});
		~ReparentEntitiesCommand() = default;

		// 複数の親変更をまとめて適用する
		bool Execute(EditorCommandContext& context) override;
		// 全対象を変更前の親へ戻す
		void Undo(EditorCommandContext& context) override;
		// 初回に決めた親へ再適用する
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Reparent Entities"; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初回の操作対象
		std::vector<Entity> targetEntities_{};
		// 変更先の親UUID
		UUID newParentStableUUID_{};
		// 適用と取消を共通Commandへ渡す
		std::unique_ptr<CompositeEditorCommand> command_;
		// 初回の適用が完了したか
		bool initialized_ = false;

		//--------- functions ----------------------------------------------------

		// 操作対象の選択へ戻す
		void RestoreSelection(EditorCommandContext& context) const;
	};
} // Engine
