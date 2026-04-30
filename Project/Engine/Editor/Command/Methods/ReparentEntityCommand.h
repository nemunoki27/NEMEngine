#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/UUID/UUID.h>

// c++
#include <vector>

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

	//============================================================================
	//	ReorderEntityCommand class
	//	同じ階層内でエンティティの表示順を変更するコマンド
	//============================================================================
	class ReorderEntityCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ReorderEntityCommand(const Entity& targetEntity, const Entity& anchorEntity, bool insertAfter);
		~ReorderEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Reorder Entity"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		Entity initialTarget_ = Entity::Null();
		Entity initialAnchor_ = Entity::Null();
		bool insertAfter_ = false;

		UUID targetStableUUID_{};
		UUID parentStableUUID_{};
		std::vector<UUID> oldOrder_{};
		std::vector<UUID> newOrder_{};

		//--------- functions ----------------------------------------------------

		// 指定した順序を適用する
		bool ApplyOrder(EditorCommandContext& context, const std::vector<UUID>& order);
	};
} // Engine
