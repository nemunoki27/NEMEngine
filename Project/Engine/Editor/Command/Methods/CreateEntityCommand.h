#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	CreateEntityCommand class
	//	エンティティを作成するコマンド
	//============================================================================
	class CreateEntityCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		explicit CreateEntityCommand(const std::string& name = "Entity", UUID parentStableUUID = UUID{});
		~CreateEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Create Entity"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::string name_;
		UUID parentStableUUID_{};
		UUID createdStableUUID_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool CreateInternal(EditorCommandContext& context);
	};
} // Engine