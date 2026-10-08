#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	ScriptExceptionListTool class
	//	Scriptの例外と呼出し位置を一覧表示する
	//============================================================================
	class ScriptExceptionListTool : public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptExceptionListTool() = default;
		~ScriptExceptionListTool() override = default;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Toolの登録情報
		ToolDescriptor descriptor_{
			.id = "engine.script_exception_list",
			.name = "Script Exception List",
			.category = "Scripting",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		bool openWindow_ = false; // 一覧の表示状態
		char textFilter_[128]{};  // 検索文字列

		//--------- functions ----------------------------------------------------

		// 一覧ウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);
	};
}
