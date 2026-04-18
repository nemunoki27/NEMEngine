#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

namespace Engine {

	//============================================================================
	//	ToolPanel class
	//	ツールパネル
	//============================================================================
	class ToolPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ToolPanel() = default;
		~ToolPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	};
} // Engine