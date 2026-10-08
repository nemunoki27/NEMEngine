#pragma once

namespace Engine {

	class AnimationClipEditSession;
	struct EditorToolContext;
}

namespace Engine::AnimationClipEditorUI {

	// 追加候補とTrack一覧を表示する
	void DrawPropertyTreeUI(AnimationClipEditSession& session, const EditorToolContext& context);
	// 選択TrackのCurveを編集する
	void DrawCurveEditorUI(AnimationClipEditSession& session, const EditorToolContext& context);
	// 選択Keyの値と補間を編集する
	void DrawKeyInspectorUI(AnimationClipEditSession& session, const EditorToolContext& context);
	// Curveの生成条件を編集する
	void DrawGeneratorUI(AnimationClipEditSession& session, const EditorToolContext& context);
	// ClipのEventを編集する
	void DrawEventListUI(AnimationClipEditSession& session);
}
