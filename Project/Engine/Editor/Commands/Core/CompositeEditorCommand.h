#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Editor/Commands/Core/EditorSelectionSnapshot.h>

// c++
#include <vector>
#include <memory>

namespace Engine {

	//============================================================================
	//	CompositeEditorCommand class
	//	複数の編集操作を1件のUndoへまとめるコマンド
	//============================================================================
	class CompositeEditorCommand final : public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CompositeEditorCommand() = delete;
		explicit CompositeEditorCommand(std::vector<std::unique_ptr<IEditorCommand>> commands);
		~CompositeEditorCommand() override = default;

		// まとめた操作を順に適用する
		bool Execute(EditorCommandContext& context) override;
		// まとめた操作を逆順で戻す
		void Undo(EditorCommandContext& context) override;
		// 固定したIDを使ってまとめた操作を再実行する
		bool Redo(EditorCommandContext& context) override;
		// 操作名を返す
		const char* GetName() const override;
	private:
		// 初回とRedoの適用・取消を共通化する
		bool Apply(EditorCommandContext& context, bool redo);
		// 途中失敗した操作を逆順で戻す
		void UndoApplied(EditorCommandContext& context, size_t count);

		std::vector<std::unique_ptr<IEditorCommand>> commands_;
		EditorSelectionSnapshot previousSelection_;
		EditorSelectionSnapshot appliedSelection_;
	};
} // Engine
