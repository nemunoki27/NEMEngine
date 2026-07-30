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
	//	RenameEntityCommand class
	//	エンティティ名変更コマンド
	//============================================================================
	class RenameEntityCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RenameEntityCommand(const Entity& targetEntity, const std::string_view& newName);
		~RenameEntityCommand() = default;

		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Rename Entity"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		Entity initialTarget_ = Entity::Null();
		UUID targetStableUUID_{};
		std::string oldName_{};
		std::string newName_{};

		bool ApplyName(EditorCommandContext& context, const std::string& name);
	};

	//============================================================================
	//	SetEntityActiveCommand class
	//	エンティティのアクティブ状態変更コマンド
	//============================================================================
	class SetEntityActiveCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SetEntityActiveCommand(const Entity& targetEntity, bool activeSelf);
		~SetEntityActiveCommand() = default;

		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Entity Active"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		Entity initialTarget_ = Entity::Null();
		UUID targetStableUUID_{};
		bool beforeActiveSelf_ = true;
		bool afterActiveSelf_ = true;

		bool Apply(EditorCommandContext& context, bool activeSelf);
	};

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

		bool Execute(EditorCommandContext& context) override;
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Entity Tag"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		Entity initialTarget_ = Entity::Null();
		UUID targetStableUUID_{};
		std::string beforeTag_ = "Untagged";
		std::string afterTag_ = "Untagged";

		bool Apply(EditorCommandContext& context, const std::string& tag);
	};
} // Engine
