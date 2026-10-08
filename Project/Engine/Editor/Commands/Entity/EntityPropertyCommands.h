#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <string_view>

namespace Engine {

	//============================================================================
	//	RenameEntityCommand class
	//	エンティティ名変更コマンド
	//============================================================================
	class RenameEntityCommand : public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RenameEntityCommand(const Entity& targetEntity, std::string_view newName);
		~RenameEntityCommand() = default;

		// 初回の値を保存して変更を適用
		bool Execute(EditorCommandContext& context) override;
		// 変更前の値へ戻す
		void Undo(EditorCommandContext& context) override;
		// 変更後の値を再適用
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Rename Entity"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初回実行時の対象
		Entity initialTarget_ = Entity::Null();
		// UndoとRedoで解決する対象UUID
		UUID targetStableUUID_{};
		// 変更前の名前
		std::string oldName_{};
		// 変更後の名前
		std::string newName_{};

		//--------- functions ----------------------------------------------------

		// 対象の名前を適用
		bool ApplyName(EditorCommandContext& context, const std::string& name);
	};

	//============================================================================
	//	SetEntityActiveCommand class
	//	エンティティのアクティブ状態変更コマンド
	//============================================================================
	class SetEntityActiveCommand : public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SetEntityActiveCommand(const Entity& targetEntity, bool activeSelf);
		~SetEntityActiveCommand() = default;

		// 初回の値を保存して変更を適用
		bool Execute(EditorCommandContext& context) override;
		// 変更前の値へ戻す
		void Undo(EditorCommandContext& context) override;
		// 変更後の値を再適用
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Entity Active"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初回実行時の対象
		Entity initialTarget_ = Entity::Null();
		// UndoとRedoで解決する対象UUID
		UUID targetStableUUID_{};
		// 変更前の有効状態
		bool beforeActiveSelf_ = true;
		// 変更後の有効状態
		bool afterActiveSelf_ = true;

		//--------- functions ----------------------------------------------------

		// 対象と子孫の有効状態を更新
		bool Apply(EditorCommandContext& context, bool activeSelf);
	};

	//============================================================================
	//	SetEntityTagCommand class
	//	エンティティのタグ変更コマンド
	//============================================================================
	class SetEntityTagCommand : public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		SetEntityTagCommand(const Entity& targetEntity, const std::string& tag);
		~SetEntityTagCommand() = default;

		// 初回の値を保存して変更を適用
		bool Execute(EditorCommandContext& context) override;
		// 変更前の値へ戻す
		void Undo(EditorCommandContext& context) override;
		// 変更後の値を再適用
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Set Entity Tag"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初回実行時の対象
		Entity initialTarget_ = Entity::Null();
		// UndoとRedoで解決する対象UUID
		UUID targetStableUUID_{};
		// 変更前のタグ
		std::string beforeTag_ = "Untagged";
		// 変更後のタグ
		std::string afterTag_ = "Untagged";

		//--------- functions ----------------------------------------------------

		// 対象のタグを適用
		bool Apply(EditorCommandContext& context, const std::string& tag);
	};
} // Engine
