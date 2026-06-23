#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	SetEntityTagCommand class
	//	エンティティのタグ変更コマンド
	//============================================================================
	class SetEntityTagCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SetEntityTagCommand(const Entity& targetEntity, const std::string& tag);
		~SetEntityTagCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Entity Tag"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID targetStableUUID_{};

		std::string beforeTag_ = "Untagged";
		std::string afterTag_ = "Untagged";

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool Apply(EditorCommandContext& context, const std::string& tag);
	};
} // Engine
