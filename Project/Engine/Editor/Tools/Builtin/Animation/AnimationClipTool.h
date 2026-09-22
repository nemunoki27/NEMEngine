#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "AnimationClipEditSession.h"
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	// front
	struct CurveChannelRef;

	//============================================================================
	//	AnimationClipTool class
	//	アニメーションの動きを作成するツール
	//============================================================================
	class AnimationClipTool :
		public IEditorTool {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		AnimationClipTool() = default;
		~AnimationClipTool() override = default;

		// ToolPanelの一覧からツールを開く
		void OpenEditorTool() override;
		// AnimationCurveウィンドウを描画する
		void DrawEditorTool(const EditorToolContext& context) override;

		//--------- accessor -----------------------------------------------------

		// ツール情報を取得する
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- variables ----------------------------------------------------

		ToolDescriptor descriptor_{
			.id = "engine.animation_clip",
			.name = "アニメクリップ作成",
			.category = "アニメーション",
			.owner = ToolOwner::Engine,
			.flags = ToolFlags::EditOnly,
			.order = 2,
		};

		bool openWindow_ = false;

		// Clipの編集とプレビューのセッション
		AnimationClipEditSession session_;

		//--------- functions ----------------------------------------------------

		// アセット、編集設定UI
		void DrawToolbarUI(const EditorToolContext& context);
		// Clipの選択と保存を表示する
		void DrawClipAssetUI(const EditorToolContext& context);
		// 編集対象と再生操作を表示する
		void DrawEditAssetUI(const EditorToolContext& context);

		// 追加できるPropertyを表示する
		void DrawPropertyTreeUI(const EditorToolContext& context);
		// 選択TrackのCurveを編集する
		void DrawCurveEditorUI(const EditorToolContext& context);
		// 選択Keyの値を編集する
		void DrawKeyInspectorUI(const EditorToolContext& context);
		// Curve生成設定を編集する
		void DrawGeneratorUI(const EditorToolContext& context);
		// ClipのEventを編集する
		void DrawEventListUI(const EditorToolContext& context);

	};
} // Engine
