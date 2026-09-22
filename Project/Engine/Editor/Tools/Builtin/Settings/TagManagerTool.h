#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	TagManagerTool class
	//	プロジェクトのタグ一覧を追加削除リネームするツール
	//============================================================================
	class TagManagerTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		TagManagerTool() = default;
		~TagManagerTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// タグ編集ウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.tag_manager",
			.name = "タグ・描画レイヤー",
			.category = "プロジェクト設定",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;
		// 追加するタグ名の入力
		char addBuffer_[128]{};
		// リネーム中のタグ名、空ならリネームしていない
		std::string renamingTag_;
		// リネーム後の入力
		char renameBuffer_[128]{};
		// 追加するRendering Layer名の入力
		char addRenderingLayerBuffer_[128]{};
		//--------- functions ----------------------------------------------------

		// ツールの編集画面を表示する
		void DrawWindow(const EditorToolContext& context);

	};
} // Engine
