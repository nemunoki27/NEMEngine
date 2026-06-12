#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	ScriptExceptionListTool class
	//	Script callbackで送出された未処理例外を一覧表示する
	//============================================================================
	class ScriptExceptionListTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptExceptionListTool() = default;
		~ScriptExceptionListTool() override = default;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 一覧ウィンドウを描画する
		void DrawWindow(const EditorToolContext& context);

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.script_exception_list",
			.name = "Script Exception List",
			.category = "Scripting",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		bool openWindow_ = false;
		char textFilter_[128]{};
	};
} // Engine
