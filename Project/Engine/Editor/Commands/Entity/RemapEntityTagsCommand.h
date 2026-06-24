#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Commands/Core/IEditorCommand.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	RemapEntityTagsCommand class
	//	開いているシーン内で特定タグを別タグへ一括付け替えするコマンド
	//============================================================================
	class RemapEntityTagsCommand :
		public IEditorCommand {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		RemapEntityTagsCommand(const std::string& fromTag, const std::string& toTag);
		~RemapEntityTagsCommand() = default;

		// コマンドの実行
		bool Execute(EditorCommandContext& context) override;

		// Undo / Redoを実行
		void Undo(EditorCommandContext& context) override;
		bool Redo(EditorCommandContext& context) override;

		//--------- accessor -----------------------------------------------------

		const char* GetName() const override { return "Remap Entity Tags"; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::string fromTag_;
		std::string toTag_;

		// 初回実行で付け替えた対象のUUID、Undoはこの対象だけを元へ戻す
		std::vector<UUID> affected_;
		bool captured_ = false;

		//--------- functions ----------------------------------------------------

		// 保存済み対象のタグを指定値へ書き換える
		bool ApplyTag(EditorCommandContext& context, const std::string& tag);
	};
} // Engine
