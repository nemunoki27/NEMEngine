#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Entity/EditorEntitySnapshot.h>
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	//============================================================================
	//	CreateDroppedEntityCommand class
	//	ドラッグ&ドロップで作成済みのエンティティをUndo/Redo対象として登録するコマンド
	//============================================================================
	class CreateDroppedEntityCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit CreateDroppedEntityCommand(const Entity& createdEntity);
		~CreateDroppedEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Create Entity (Drop)"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 既にD&Dで作成済みのエンティティ
		Entity createdEntity_ = Entity::Null();
		UUID targetStableUUID_{};

		// 作成したエンティティのサブツリーのスナップショット
		EditorEntityTreeSnapshot snapshot_{};
		// スナップショット取得済みか
		bool captured_ = false;
	};
} // Engine
