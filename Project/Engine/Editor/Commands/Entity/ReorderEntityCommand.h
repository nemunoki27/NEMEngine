#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	ReorderEntityCommand class
	//	同じ階層の兄弟順を変更する
	//============================================================================
	class ReorderEntityCommand : public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ReorderEntityCommand(const Entity& targetEntity, const Entity& anchorEntity, bool insertAfter);
		~ReorderEntityCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// 変更を取り消して再適用する
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Reorder Entity"; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// 初回の並替え対象
		Entity initialTarget_ = Entity::Null();
		// 挿入位置の基準Entity
		Entity initialAnchor_ = Entity::Null();
		// 基準Entityの後ろに挿入するか
		bool insertAfter_ = false;

		// 並替え対象のUUID
		UUID targetStableUUID_{};
		// 初回の親UUID
		UUID parentStableUUID_{};
		// 変更前の兄弟順
		std::vector<UUID> oldOrder_{};
		// 変更後の兄弟順
		std::vector<UUID> newOrder_{};

		//--------- functions ----------------------------------------------------

		// 指定した順序を適用する
		bool ApplyOrder(EditorCommandContext& context, const std::vector<UUID>& order);
	};
} // Engine
