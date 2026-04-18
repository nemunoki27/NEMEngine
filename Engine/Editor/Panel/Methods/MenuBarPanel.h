#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

namespace Engine {

	//============================================================================
	//	MenuBarPanel class
	//	メニューバーパネル
	//============================================================================
	class MenuBarPanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		MenuBarPanel() = default;
		~MenuBarPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	};
} // Engine