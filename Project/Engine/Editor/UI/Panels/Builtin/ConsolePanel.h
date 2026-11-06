#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

namespace Engine {

	//============================================================================
	//	ConsolePanel class
	//	コンソールパネル
	//============================================================================
	class ConsolePanel :
		public IEditorPanel {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		ConsolePanel() = default;
		~ConsolePanel() = default;

		void Draw(const EditorPanelContext& context) override;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		std::vector<Logger::LogEntry> logs_;
		std::vector<float> logHeights_;
		uint64_t logRevision_ = 0;
		float logWidth_ = 0.0f;
		float logFontSize_ = 0.0f;
		ImFont* logFont_ = nullptr;

		// ログ更新時と表示幅の変更時だけ表示データを作り直す
		void RefreshLogs(float width);
		// ログ一覧と保存先メニューを描画する
		void DrawLogs();
	};
} // Engine
