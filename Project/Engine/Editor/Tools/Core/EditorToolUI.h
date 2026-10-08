#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>

//============================================================================
//	EditorToolUI namespace
//============================================================================
namespace Engine::EditorToolUI {

	// メニューバー内で呼ぶ。ツール一覧と起動操作を担当する。
	void DrawMenu(const EditorPanelContext& context);

	// シーン描画後、ImGuiフレームの終了前に毎フレーム呼ぶ。
	void DrawWindows(const EditorPanelContext& context);

	// 独立ツールの未確定編集を取得する
	bool HasPendingEdits();
	// 各ToolのSceneプレビューを終了する
	void EndScenePreviews();
	void RequestResolvePendingEdits();
	EditorToolCloseResult ConsumePendingEditCloseResult();
}
