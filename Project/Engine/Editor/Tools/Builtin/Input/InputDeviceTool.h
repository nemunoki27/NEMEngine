#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	InputDeviceTool class
	//	入力タイプの自動更新、検知入力、デッドゾーン、マウス範囲制御を編集するツール
	//============================================================================
	class InputDeviceTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		InputDeviceTool() = default;
		~InputDeviceTool() override = default;

		void Tick(ToolContext& context) override;
		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private variables
		//============================================================================

		// ToolPanelへ登録する情報
		ToolDescriptor descriptor_{
			.id = "engine.input_device",
			.name = "入力デバイス",
			.category = "入力",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::AllowPlayMode,
			.order = 0,
		};

		// ウィンドウ表示状態
		bool openWindow_ = false;

		// 検知入力の追加用ドラフト
		int draftDevice_ = 0;
		bool draftIsMovement_ = false;
		int32_t draftCode_ = 0;

		//--------- functions ----------------------------------------------------

		void DrawWindow();
	};
} // Engine
