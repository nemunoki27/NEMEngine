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

		// Toolパネル内にツールUIを描画
		virtual void DrawEditorTool(const EditorToolContext& context) = 0;
	};
} // Engine
