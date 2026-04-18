#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	SetEntityActiveCommand class
	//	エンティティのアクティブ状態変更コマンド
	//============================================================================
	class SetEntityActiveCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		SetEntityActiveCommand(const Entity& targetEntity, bool activeSelf);
		~SetEntityActiveCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Entity Active"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();

		UUID targetStableUUID_{};

		bool beforeActiveSelf_ = true;
		bool afterActiveSelf_ = true;

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool Apply(EditorCommandContext& context, bool activeSelf);
	};
} // Engine