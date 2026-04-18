#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Command/Interface/IEditorCommand.h>
#include <Engine/Editor/Command/EditorEntitySnapshot.h>
#include <Engine/Core/UUID/UUID.h>

namespace Engine {

	//============================================================================
	//	PasteEntityTreeCommand class
	//	クリップボードのサブツリーをワールドに貼り付けるコマンド
	//============================================================================
	class PasteEntityTreeCommand :
		public IEditorCommand {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PasteEntityTreeCommand(const EditorEntityTreeSnapshot& sourceSnapshot, UUID parentStableUUID);
		~PasteEntityTreeCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Paste Entity Tree"; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 貼り付け元のエンティティツリーのスナップショット
		EditorEntityTreeSnapshot sourceSnapshot_{};
		EditorEntityTreeSnapshot preparedSnapshot_{};

		UUID parentStableUUID_{};
		UUID createdRootStableUUID_{};

		//--------- functions ----------------------------------------------------

		// コマンドの実行処理
		bool PasteInternal(EditorCommandContext& context);
	};
} // Engine