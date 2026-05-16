#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Panel/Interface/IEditorPanel.h>

// c++
#include <string>

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

		// RenderTextureプレビューは描画パイプラインの結果を使うため、Scene/Game描画後に処理する
		EditorPanelPhase GetPhase() const override { return EditorPanelPhase::PostScene; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::string activeToolID_{};
	};
} // Engine
