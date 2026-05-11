#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Curve/AnimationCurve.h>
#include <Engine/Editor/Animation/CurveEditorState.h>
#include <Engine/Editor/Tools/Interface/IEditorTool.h>

namespace Engine {

	enum class AnimationCurveToolSampleType {

		Transform,
		Color,
		Quaternion
	};

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
			.category = "AnimationClip",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 1,
		};

		bool openWindow_ = false;
		AnimationCurveToolSampleType sampleType_ = AnimationCurveToolSampleType::Transform;
		CurveVector3 transformCurve_;
		CurveColor4 colorCurve_;
		CurveQuaternion quaternionCurve_;
		CurveEditorState transformState_;
		CurveEditorState colorState_;
		CurveEditorState quaternionState_;

		//--------- functions ----------------------------------------------------

		void DrawWindow(const EditorToolContext& context);
		void SetupSampleCurve();
	};
} // Engine
