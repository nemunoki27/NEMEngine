#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	ReparentEntityCommand class
	//	エンティティの親を変更するコマンド
	//============================================================================
	class ReparentEntityCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit ReparentEntityCommand(const Entity& targetEntity, UUID newParentStableUUID = UUID{});
		~ReparentEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Reparent Entity"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID targetStableUUID_{};
		UUID oldParentStableUUID_{};
		UUID newParentStableUUID_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool ApplyParent(EditorCommandContext& context, UUID parentStableUUID);
	};
} // Engine