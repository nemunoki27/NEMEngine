#pragma once

namespace Engine {

	class ShaderGraphEditSession;
	class ShaderGraphPreviewController;
	struct EditorToolContext;

	//============================================================================
	//	ShaderGraphSettingsEditor class
	//	Graphの描画設定を編集sessionへ渡す
	//============================================================================
	class ShaderGraphSettingsEditor {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		ShaderGraphSettingsEditor(ShaderGraphEditSession& session, ShaderGraphPreviewController& preview);

		// 設定を表示して描画対象の変更を返す
		bool Draw();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ShaderGraphEditSession& editSession_;
		ShaderGraphPreviewController& previewController_;
	};
} // Engine
