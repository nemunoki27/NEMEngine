#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>

// c++
#include <filesystem>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	MenuBarPanel class
	//	メニューバーパネル
	//============================================================================
	class MenuBarPanel :
		public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		MenuBarPanel() = default;
		~MenuBarPanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		std::string layoutNameBuffer_;
		std::string layoutSaveError_;
		bool requestOpenLayoutSavePopup_ = false;

		//--------- functions ----------------------------------------------------

		// エディターレイアウト設定メニューを描画
		void DrawEditorLayoutMenu(const EditorPanelContext& context);
		// レイアウト名入力Popupを描画
		void DrawLayoutSavePopup(const EditorPanelContext& context);
	};
} // Engine
