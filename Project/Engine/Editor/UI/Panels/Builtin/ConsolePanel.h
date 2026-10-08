#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/IEditorPanel.h>
#include <Engine/Core/Foundation/Time/ProfileCapture.h>
#include <json.hpp>

// c++
#include <array>

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

		//--------- variables ----------------------------------------------------

		int32_t captureFrameLimit_ = 600;
		std::array<char, 512> capturePath_{ "capture.profile.json" };
		nlohmann::json comparisonCapture_{};
		std::string captureStatus_{};

		//--------- functions ----------------------------------------------------

		// 記録と保存済み結果の比較操作を表示する
		void DrawCaptureControls();
	};
} // Engine
