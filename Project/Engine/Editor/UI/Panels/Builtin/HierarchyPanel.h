#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include "HierarchyEntityTree.h"

namespace Engine {

	// front
	class TextureUploadService;

	//============================================================================
	//	HierarchyPanel class
	//	ヒエラルキーパネル
	//============================================================================
	class HierarchyPanel : public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		explicit HierarchyPanel(TextureUploadService& textureUploadService);
		~HierarchyPanel() = default;

		// SceneごとのEntity一覧を表示する
		void Draw(const EditorPanelContext& context) override;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Entity階層の検索と表示状態
		HierarchyEntityTree entityTree_;

		//--------- functions ----------------------------------------------------

		// 背景の作成と貼り付けメニューを表示する
		void DrawBackgroundContextMenu(const EditorPanelContext& context);
	};
} // Engine
