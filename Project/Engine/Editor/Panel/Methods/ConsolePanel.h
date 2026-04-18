#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

namespace Engine {

	//============================================================================
	//	ConsolePanel class
	//	コンソールパネル
	//============================================================================
	class ConsolePanel :
		public IEditorPanel {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ConsolePanel() = default;
		~ConsolePanel() = default;

		void Draw(const EditorPanelContext& context) override;
	};
} // Engine