#pragma once

namespace Engine {

	class ShaderGraphEditSession;
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

		explicit ShaderGraphSettingsEditor(ShaderGraphEditSession& session);

		// 設定を表示して描画対象の変更を返す
		bool Draw();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ShaderGraphEditSession& editSession_;
	};
} // Engine
