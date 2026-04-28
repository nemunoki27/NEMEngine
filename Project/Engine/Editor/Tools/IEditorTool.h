#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ITool.h>
#include <Engine/Editor/Tools/EditorToolContext.h>

namespace Engine {

	//============================================================================
	//	IEditorTool class
	//	ImGuiで操作するエディタツールのインターフェース
	//============================================================================
	class IEditorTool :
		public ITool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		IEditorTool() = default;
		~IEditorTool() override = default;

		// ToolPanelの一覧からツールを開く
		virtual void OpenEditorTool() {}
		// 独立したエディタウィンドウを描画する
		virtual void DrawEditorTool(const EditorToolContext& context) = 0;
	};
} // Engine
