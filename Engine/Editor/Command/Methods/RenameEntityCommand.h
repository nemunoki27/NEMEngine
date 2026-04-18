#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	RenameEntityCommand class
	//	エンティティ名変更コマンド
	//============================================================================
	class RenameEntityCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		RenameEntityCommand(const Entity& targetEntity, const std::string_view& newName);
		~RenameEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Rename Entity"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID targetStableUUID_{};

		std::string oldName_{};
		std::string newName_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool ApplyName(EditorCommandContext& context, const std::string& name);
	};
} // Engine