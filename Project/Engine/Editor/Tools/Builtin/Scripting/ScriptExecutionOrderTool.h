#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	ScriptExecutionOrderTool class
	//	Script Type GUID単位の実行順を閲覧編集するツール
	//============================================================================
	class ScriptExecutionOrderTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptExecutionOrderTool() = default;
		~ScriptExecutionOrderTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// 実行順編集ウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawWindow(const EditorToolContext& context);

		//--------- variables ----------------------------------------------------

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.script_execution_order",
			.name = "ScriptExecutionOrder",
			.category = "Scripting",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;
		// 検索フィルタ、型名の部分一致
		char searchBuffer_[128]{};
		// 未保存の編集があるか
		bool dirty_ = false;
	};
} // Engine
