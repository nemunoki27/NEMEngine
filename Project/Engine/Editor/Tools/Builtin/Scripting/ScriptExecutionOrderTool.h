#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/UI/Common/TextSearchFilter.h>

namespace Engine {

	//============================================================================
	//	ScriptExecutionOrderTool class
	//	C#スクリプト型ごとの実行順をProjectSettingsで上書きするツール
	//============================================================================
	class ScriptExecutionOrderTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ScriptExecutionOrderTool() = default;
		~ScriptExecutionOrderTool() override = default;

		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		void DrawWindow(const EditorToolContext& context);

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.script_execution_order",
			.name = "スクリプト実行順",
			.category = "スクリプト",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 0,
		};

		TextSearchFilter searchFilter_;
		bool openWindow_ = false;
		bool dirty_ = false;
	};
} // Engine
