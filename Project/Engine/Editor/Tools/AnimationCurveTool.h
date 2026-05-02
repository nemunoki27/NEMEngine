#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Curve/AnimationCurve.h>
#include <Engine/Editor/Animation/CurveEditorState.h>
#include <Engine/Editor/Tools/IEditorTool.h>

namespace Engine {

	//============================================================================
	//	AnimationCurveTool class
	//	アニメーション用カーブ編集の確認ツール
	//============================================================================
	class AnimationCurveTool :
		public IEditorTool {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AnimationCurveTool();
		~AnimationCurveTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// AnimationCurveウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.animation_curve",
			.name = "AnimationCurve",
			.category = "Animation",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 1,
		};

		bool openWindow_ = false;
		CurveVector3 transformCurve_;
		CurveColor4 colorCurve_;
		CurveEditorState transformState_;
		CurveEditorState colorState_;
		bool showColorCurve_ = false;

		//--------- functions ----------------------------------------------------

		void DrawWindow(const EditorToolContext& context);
		void SetupSampleCurve();
	};
} // Engine
