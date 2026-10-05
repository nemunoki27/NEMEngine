#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include "EditorLayoutMenuSession.h"

namespace Engine {

	//============================================================================
	//	MenuBarPanel class
	//	メニューバーパネル
	//============================================================================
	class MenuBarPanel : public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MenuBarPanel() = default;
		~MenuBarPanel() = default;

		// 操作と設定のメニューを表示する
		void Draw(const EditorPanelContext& context) override;

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// レイアウト名の入力と保存要求
		EditorLayoutMenuSession layoutSession_;
	};
} // Engine
