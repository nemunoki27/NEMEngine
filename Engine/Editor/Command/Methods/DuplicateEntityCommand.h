#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Editor/Command/EditorEntitySnapshot.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	DuplicateEntityCommand class
	//	指定エンティティのサブツリーを複製するコマンド
	//============================================================================
	class DuplicateEntityCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit DuplicateEntityCommand(const Entity& targetEntity);
		~DuplicateEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Duplicate Entity"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID sourceStableUUID_{};
		UUID sourceParentStableUUID_{};
		UUID createdRootStableUUID_{};

		// 複製元のエンティティツリーのスナップショット
		EditorEntityTreeSnapshot preparedSnapshot_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool DuplicateInternal(EditorCommandContext& context);
	};
} // Engine