#include "AnimationClipTool.h"
#include "AnimationClipEditorUtility.h"
#include "AnimationClipEditorUI.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
// c++
#include <algorithm>

// imgui
#include <imgui_internal.h>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

//============================================================================
//	AnimationClipTool classMethods
//============================================================================

void AnimationClipTool::OpenEditorTool() {

	// ウィンドウ起動
	openWindow_ = true;
}

void AnimationClipTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		// 閉じたToolのプレビューを解除する
		session_.EndPreviewAndRestore(context);
		return;
	}

	if (!context.CanEditScene() || context.IsPlaying()) {

		session_.EndPreviewAndRestore();
		DrawPendingEdits(context);
		return;
	}
	if (!ImGui::Begin("アニメーションクリップ作成ツール", &openWindow_)) {
		ImGui::End();
		// 折りたたみ中も未保存編集の確認を残す
		session_.EndPreviewAndRestore(context);
		if (!openWindow_ && HasPendingEdits()) {
			openWindow_ = true;
			pendingClose_ = true;
		}
		DrawPendingEdits(context);
		return;
	}

	// GizmoとUndoによる外部編集を検出する
	session_.UpdateExternalEdits(context);

	//============================================================================
	//	AnimationClip編集UI
	//============================================================================
	if (ImGui::BeginTable("AnimationClipToolTopLayout", 2,
			ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Toolbar", ImGuiTableColumnFlags_WidthFixed, 420.0f);
		ImGui::TableSetupColumn("Properties", ImGuiTableColumnFlags_WidthStretch);

		ImGui::TableNextColumn();
		// アセット、編集設定UI
		DrawToolbarUI(context);

		ImGui::TableNextColumn();
		// プロパティ設定UI
		AnimationClipEditorUI::DrawPropertyTreeUI(session_, context);

		ImGui::EndTable();
	}

	ImGui::Separator();

	if (ImGui::BeginTable("AnimationClipToolEditLayout", 2,
			ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp)) {

		ImGui::TableSetupColumn("Curve", ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("KeyInspector", ImGuiTableColumnFlags_WidthFixed, 340.0f);

		ImGui::TableNextColumn();
		// プロパティカーブ編集UI
		AnimationClipEditorUI::DrawCurveEditorUI(session_, context);

		ImGui::TableNextColumn();
		AnimationClipEditorUI::DrawKeyInspectorUI(session_, context);
		AnimationClipEditorUI::DrawGeneratorUI(session_, context);
		AnimationClipEditorUI::DrawEventListUI(session_);

		ImGui::EndTable();
	}

	// 対象Entityへプレビューを反映する
	session_.UpdatePreviewPlayback(context, ImGui::GetIO().DeltaTime);

	ImGui::End();

	if (!openWindow_) {

		session_.EndPreviewAndRestore(context);
		if (HasPendingEdits()) {
			openWindow_ = true;
			pendingClose_ = true;
		}
	}
	DrawPendingEdits(context);
}

void AnimationClipTool::DrawToolbarUI(const EditorToolContext& context) {

	// 元のフォントサイズを取得
	float beforeFontScale = ImGui::GetCurrentWindow()->FontWindowScale;
	ImGui::SetWindowFontScale(0.8f);

	//============================================================================
	//	アニメアセットの設定
	//============================================================================
	DrawClipAssetUI(context);

	//============================================================================
	//	再生・編集設定
	//============================================================================
	DrawEditAssetUI(context);

	ImGui::SetWindowFontScale(beforeFontScale);
}

void AnimationClipTool::DrawClipAssetUI(const EditorToolContext& context) {

	AssetID selected = session_.GetClipAssetID();

	//============================================================================
	//	アセット設定・保存
	//============================================================================
	{
		MyGUI::BeginPropertyRow("アニメクリップのセット");

		// アニメーションクリップアセットのセット
		ValueEditResult result =
			MyGUI::AssetReferenceField("", selected, context.toolContext.assetDatabase, {AssetType::AnimationClip},
				{.useAutoPropertyRow = false,
					.buttonSize = ImVec2(ImGui::GetContentRegionAvail().x / 2.0f, ImGui::GetFrameHeight())});
		ImGui::SameLine();
		// 保存ボタン
		if (ImGui::Button("保存", ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()))) {

			session_.SaveClipToSelectedAsset(context);
		}
		MyGUI::EndPropertyRow();

		// アセットファイルが変更されたとき
		if (result.valueChanged) {
			RequestClipSwitch(context, selected);
		}

		if (!session_.GetClipErrorText().empty()) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
			ImGui::TextWrapped("%s", session_.GetClipErrorText().c_str());
			ImGui::PopStyleColor();
		}
	}
	//============================================================================
	//	アニメ対象エンティティのセット
	//============================================================================
	{
		ECSWorld* world = context.GetWorld();
		UUID nextTargetUUID = session_.GetTargetEntityUUID();

		MyGUI::BeginPropertyRow("対象エンティティのセット");

		const float clearButtonWidth = 96.0f;
		const float entityFieldWidth =
			(std::max)(ImGui::GetContentRegionAvail().x - clearButtonWidth - ImGui::GetStyle().ItemSpacing.x, 1.0f);
		const ValueEditResult result = MyGUI::EntityReferenceField("", nextTargetUUID, world,
			{.useAutoPropertyRow = false, .buttonSize = ImVec2(entityFieldWidth, ImGui::GetFrameHeight())});
		if (result.valueChanged && nextTargetUUID != session_.GetTargetEntityUUID()) {
			session_.SetPreviewTarget(context, nextTargetUUID);
		}
		ImGui::SameLine();
		const bool hasTargetEntity = session_.GetTargetEntityUUID() != UUID{};
		if (!hasTargetEntity) {
			ImGui::BeginDisabled();
		}
		if (ImGui::Button("解除", ImVec2(clearButtonWidth, ImGui::GetFrameHeight()))) {
			session_.ClearPreviewTarget(context);
		}
		if (!hasTargetEntity) {
			ImGui::EndDisabled();
		}
		MyGUI::EndPropertyRow();
	}
	//============================================================================
	//	アニメーションの再生設定
	//============================================================================
	// アセットが設定されていなければ処理しない
	if (!session_.GetHasClip()) {
		return;
	}
	{
		// アニメーションを再生する長さ
		float duration = session_.GetClip().duration;
		auto result = MyGUI::DragFloat("再生時間", duration,
			{.dragSpeed = 0.01f,
				.minValue = 0.01f,
				.maxValue = 10000.0f,
				.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f});
		if (result.valueChanged) {
			// 必ず0.0f以上に制限する
			session_.GetClip().duration = (std::max)(duration, 0.01f);
			session_.GetPreviewTime() = (std::clamp)(session_.GetPreviewTime(), 0.0f, session_.GetClip().duration);
			session_.GetCurveState().visibleTimeMax =
				(std::max)(session_.GetCurveState().visibleTimeMax, session_.GetClip().duration);
			session_.MarkClipDirty();
		}
		if (MyGUI::Checkbox("最後のキーを再生時間に設定", session_.GetClip().autoDuration)) {

			UpdateAnimationClipAutoDuration(session_.GetClip());
			session_.GetPreviewTime() =
				(std::clamp)(session_.GetPreviewTime(), 0.0f, AnimationClipEvaluator::GetPlaybackDuration(session_.GetClip()));
			session_.MarkClipDirty();
		}

		// 再生開始時の向きから位置と回転を適用する
		if (MyGUI::Checkbox("向き相対(位置/回転)", session_.GetClip().relativeTransform)) {
			// 適用方法を変える前に元の値へ戻す
			session_.EndPreviewAndRestore(context);
			session_.MarkClipDirty();
		}

		ImGui::SeparatorText("ループ再生についての設定");

		if (MyGUI::Checkbox("ループ再生", session_.GetClip().loop)) {
			session_.MarkClipDirty();
		}
		// ループ再生する場合のみの設定
		if (session_.GetClip().loop) {

			bool bridgeEnabled = session_.GetClip().loopBridge.enabled;
			if (MyGUI::Checkbox("ループのつなぎ補間", bridgeEnabled)) {
				session_.GetClip().loopBridge.enabled = bridgeEnabled;
				session_.MarkClipDirty();
			}
			if (session_.GetClip().loopBridge.enabled) {

				result = {};
				result = MyGUI::DragFloat("補間時間", session_.GetClip().loopBridge.duration,
					{.dragSpeed = 0.001f,
						.minValue = 0.001f,
						.maxValue = 10.0f,
						.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f});
				if (result.valueChanged) {
					session_.GetClip().loopBridge.duration = (std::max)(session_.GetClip().loopBridge.duration, 0.001f);
					session_.MarkClipDirty();
				}
				result = {};
				result = MyGUI::EnumCombo<CurveInterpolationMode>("補間方法", session_.GetClip().loopBridge.interpolation,
					{.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f});
				if (result.valueChanged) {
					session_.MarkClipDirty();
				}
			}
		}
	}
}

void Engine::AnimationClipTool::DrawEditAssetUI(const EditorToolContext& context) {

	ImGui::SeparatorText("アニメ編集設定");

	MyGUI::EnumCombo(
		"編集次元の設定", session_.GetEditDimension(), {.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f});

	if (!session_.GetHasClip()) {
		return;
	}

	auto result = MyGUI::DragFloat("現在の時間", session_.GetPreviewTime(),
		{.dragSpeed = 0.01f,
			.minValue = 0.0f,
			.maxValue = session_.GetClip().duration,
			.closeOnProperty = false,
			.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f});
	if (result.valueChanged) {

		// Scrub中もSceneViewへ即反映し、カーブ編集結果を確認できるようにする
		session_.GetPreviewTime() = (std::clamp)(session_.GetPreviewTime(), 0.0f, session_.GetClip().duration);
		session_.GetCurveState().currentTime = session_.GetPreviewTime();
		session_.ApplyPreviewAtCurrentTime(context, true);
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%.3f / %.3f", session_.GetPreviewTime(), session_.GetClip().duration);

	MyGUI::EndPropertyRow();

	MyGUI::DragFloat("再生速度", session_.GetPreviewSpeed(),
		{.dragSpeed = 0.01f,
			.minValue = 0.01f,
			.maxValue = 8.0f,
			.reserveRightWidth = ImGui::GetContentRegionAvail().x / 2.0f});

	// 再生/ポーズボタン
	if (ImGui::Button(session_.GetPreviewPlaying() ? "ポーズ" : "再生", ImVec2(100.f, ImGui::GetFrameHeight()))) {
		session_.TogglePreviewPlayback(context);
	}
	ImGui::SameLine();
	// 停止ボタン
	if (ImGui::Button("停止", ImVec2(100.f, ImGui::GetFrameHeight()))) {
		session_.StopPreviewPlayback(context);
	}
}
