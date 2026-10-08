#pragma once

namespace Engine {

	struct EditorPanelContext;

	// GPU能力と希望設定を表示する
	namespace EditorGraphicsMenu {

		// 描画機能の設定を編集する
		void Draw(const EditorPanelContext& context);
	} // EditorGraphicsMenu
} // Engine
