#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Panels/Core/EditorPanelContext.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include <Engine/Editor/Core/Layout/EditorLayoutTypes.h>

//============================================================================
//	EditorToolUI namespace
//============================================================================
namespace Engine::EditorToolUI {

	// メニューバー内で呼ぶ。ツール一覧と起動操作を担当する。
	void DrawMenu(const EditorPanelContext& context);
	// プロジェクト設定の起動項目を指定順で表示する
	void DrawProjectSettingsMenu(const EditorPanelContext& context);

	// シーン描画後、ImGuiフレームの終了前に毎フレーム呼ぶ。
	void DrawWindows(const EditorPanelContext& context);
	// 登録されたツールへInspectorの描画を委譲する
	bool DrawInspection(const EditorPanelContext& context, const EditorInspectionTarget& target);
	bool IsInspectionTargetValid(const EditorInspectionTarget& target);

	// 登録済みツールの開閉状態を保存する
	std::vector<EditorToolLayoutSnapshot> CaptureLayoutState();
	// 保存されたIDに対応するツールの開閉状態を復元する
	void ApplyLayoutState(const std::vector<EditorToolLayoutSnapshot>& tools);

	// 独立ツールの未確定編集を取得する
	bool HasPendingEdits();
	// 各ToolのSceneプレビューを終了する
	void EndScenePreviews();
	// 新しく開始するプレビュー以外の対象値を復元する
	void EndOtherScenePreviews(std::string_view exceptID);
	void RequestResolvePendingEdits();
	EditorToolCloseResult ConsumePendingEditCloseResult();
}
