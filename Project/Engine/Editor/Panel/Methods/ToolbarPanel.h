#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

namespace Engine {

	//============================================================================
	//	ToolbarPanel class
	//	ツールバーパネル
	//============================================================================
	class ToolbarPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ToolbarPanel() = default;
		~ToolbarPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	};
} // Engine