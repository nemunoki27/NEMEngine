#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/EditorEntitySnapshot.h>
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	DeleteEntityCommand class
	//	エンティティを削除するコマンド
	//============================================================================
	class DeleteEntityCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit DeleteEntityCommand(const Entity& targetEntity);
		~DeleteEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Delete Entity"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();
		UUID targetStableUUID_{};
		UUID parentStableUUIDBeforeDelete_{};

		// 削除するエンティティのサブツリーのスナップショット
		EditorEntityTreeSnapshot snapshot_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool DeleteInternal(EditorCommandContext& context);
	};
} // Engine