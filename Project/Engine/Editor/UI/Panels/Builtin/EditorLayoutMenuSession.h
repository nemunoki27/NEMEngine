#pragma once

// c++
#include <string>

namespace Engine {

	struct EditorPanelContext;

	//============================================================================
	//	EditorLayoutMenuSession class
	//	レイアウトの選択と保存入力を管理する
	//============================================================================
	class EditorLayoutMenuSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 保存要求とレイアウト一覧を表示する
		void DrawMenu(const EditorPanelContext& context);
		// レイアウト名と保存失敗を表示する
		void DrawPopup(const EditorPanelContext& context);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// 入力中のレイアウト名
		std::string layoutNameBuffer_;
		// 保存失敗の表示
		std::string layoutSaveError_;
		// 保存Popupの開始要求
		bool requestOpenLayoutSavePopup_ = false;
	};
} // Engine
