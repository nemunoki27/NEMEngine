#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include "EditorLayoutMenuSession.h"
#include <Engine/Editor/Templates/EditorDeveloperLauncher.h>

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

		explicit MenuBarPanel(EditorDeveloperLauncher& launcher) : developerLauncher_(launcher) {}
		~MenuBarPanel() override = default;

		// 操作と設定のメニューを表示する
		void Draw(const EditorPanelContext& context) override;

		//--------- accessor -----------------------------------------------------

		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PreDockSpace; }

	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		// レイアウト名の入力と保存要求
		EditorLayoutMenuSession layoutSession_;
		EditorDeveloperLauncher& developerLauncher_;
	};
} // Engine
