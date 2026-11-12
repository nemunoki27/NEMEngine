#include "TimelineTool.h"
#include "TimelineEditorFields.h"
#include "TimelineAppearance.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Tools/Core/EditorToolUI.h>

// c++
#include <algorithm>

using namespace Engine;

//============================================================================
//	TimelineTool methods
//============================================================================
void TimelineTool::Tick(ToolContext& context) {

	if (!openWindow_ || !visibleLastFrame_ || pending_ || context.isPlaying || !context.canEditScene ||
		!context.world || !context.systemContext) {
		recording_.EndRecording();
		session_.EndPreview();
		return;
	}
	EditorToolContext editor;
	editor.toolContext = context;
	if (session_.previewEnabled && !session_.IsPreviewActive() && context.world->IsAlive(session_.GetOwner(editor)))
		EditorToolUI::EndOtherScenePreviews(descriptor_.id);
	if (!AnimationPropertyEditCapture::IsEditing(*context.world)) session_.UpdatePreview(editor, context.deltaTime);
}

void TimelineTool::OpenEditorTool() {

	openWindow_ = true;
}

void TimelineTool::OpenAsset(AssetID assetID) {

	queuedAsset_ = assetID;
	queuedAssetOnly_ = true;
	openWindow_ = true;
}

void TimelineTool::OpenPlayer(AssetID assetID, UUID owner) {

	OpenAsset(assetID);
	queuedAssetOnly_ = false;
	queuedOwner_ = owner;
}

bool TimelineTool::HasPendingEdits() const {

	return session_.IsDirty() || recording_.HasPendingEdits();
}

void TimelineTool::EndScenePreview() {

	ResetInteraction();
	visibleLastFrame_ = false;
	recording_.EndRecording();
	session_.EndPreview();
}

void TimelineTool::RequestResolvePendingEdits() {

	openWindow_ = true;
	resolveClose_ = true;
	closeResult_ = EditorToolCloseResult::None;
	pending_ = PendingAction{ PendingKind::Close };
}

EditorToolCloseResult TimelineTool::ConsumePendingEditCloseResult() {

	const auto result = closeResult_;
	closeResult_ = EditorToolCloseResult::None;
	return result;
}

void TimelineTool::DrawEditorTool(const EditorToolContext& context) {

	visibleLastFrame_ = false;
	if (!openWindow_) { session_.EndPreview(); return; }
	const auto worldLifetime = context.GetWorld() ? context.GetWorld()->GetLifetime() : nullptr;
	if (interactionWorld_.lock() != worldLifetime) { ResetInteraction(); interactionWorld_ = worldLifetime; }
	FollowSelection(context);
	if (queuedAsset_) {
		const AssetID assetID = *queuedAsset_;
		queuedAsset_.reset();
		if (assetID != session_.GetAssetID()) RequestAction(context, { PendingKind::Load, assetID, {}, queuedAssetOnly_ });
		else if (queuedAssetOnly_) { ResetInteraction(); session_.SetOwner(context, {}); SelectInspection(context); }
	}
	if (queuedOwner_ && !pending_) {
		ResetInteraction();
		session_.SetOwner(context, queuedOwner_);
		queuedOwner_ = {};
	}
	session_.SyncExternalAsset(context, !ImGui::IsAnyItemActive() && dragMode_ == DragMode::None);
	session_.SyncPlayback(context);
	ImGui::SetNextWindowSize(ImVec2(1240, 720), ImGuiCond_FirstUseEver);
	TimelineAppearance::PushStyle();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2.0f, 2.0f));
	const bool visible = EditorWindowPresentation::Begin(MakeWindowName("タイムライン").c_str(), &openWindow_);
	ImGui::PopStyleVar();
	if (!visible) {
		ImGui::End();
		TimelineAppearance::PopStyle();
		session_.EndPreview();
		if (!openWindow_ && HasPendingEdits()) { openWindow_ = true; pending_ = PendingAction{ PendingKind::Close }; }
		DrawPending(context);
		return;
	}
	visibleLastFrame_ = true;
	if (context.panelContext && context.panelContext->host && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
		context.panelContext->host->NotifyEditorCommandPanelFocused(EditorCommandPanelKind::Tool);
	if (context.IsPlaying()) { recording_.EndRecording(); session_.EndPreview(); session_.SyncPlayback(context); }
	recording_.Update(context, session_.GetAsset(), session_.time, session_.playing);
	NormalizeSelection();
	ImGui::PushFont(nullptr, ImGui::GetFontSize() * 0.75f);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f, 4.0f));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 2.0f));
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
		!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) session_.RestoreHistory(false);
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
			session_.RestoreHistory(true);
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) SaveAll(context);
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C)) CopySelection();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V)) PasteSelection();
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) DuplicateSelection();
		if (ImGui::Shortcut(ImGuiKey_Delete)) DeleteSelection();
		if (ImGui::Shortcut(ImGuiKey_Space)) session_.TogglePlayback(context);
		if (ImGui::Shortcut(ImGuiKey_Comma)) session_.Step(context, -1);
		if (ImGui::Shortcut(ImGuiKey_Period)) session_.Step(context, 1);
		if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Comma)) session_.Seek(context, 0.0);
		if (ImGui::Shortcut(ImGuiMod_Shift | ImGuiKey_Period)) session_.Seek(context, session_.GetAsset().duration);
		if (ImGui::Shortcut(ImGuiKey_F)) FrameSelection(false);
		if (ImGui::Shortcut(ImGuiKey_A)) FrameSelection(true);
		if (ImGui::Shortcut(ImGuiKey_S) && !ImGui::GetIO().KeyCtrl) clipAction_ = ClipAction::Split;
	}
	DrawToolbar(context);
	recording_.DrawCreateDialog(context, session_.GetAsset());
	if (recording_.ConsumeAssetChanged()) {
		session_.MarkChanged();
		if (recording_.IsRecording()) {
			targetLocked_ = true;
			for (const auto& track : session_.GetAsset().tracks) if (recording_.IsRecording(track.id) && context.panelContext)
				context.panelContext->editorState->SelectEntity(ResolveTrackTarget(context, track));
		}
	}
	ImGui::Separator();
	if (session_.HasAsset()) {
		DrawCanvas(context);
	}
	if (!ImGui::IsAnyItemActive() && dragMode_ == DragMode::None) session_.CommitHistory();
	session_.PublishDraft(context);
	ImGui::PopStyleVar(2);
	ImGui::PopFont();
	ImGui::End();
	TimelineAppearance::PopStyle();
	if (!openWindow_) {
		recording_.EndRecording();
		session_.EndPreview();
		if (HasPendingEdits()) { openWindow_ = true; pending_ = PendingAction{ PendingKind::Close }; }
	}
	DrawPending(context);
	if (!openWindow_) visibleLastFrame_ = false;
}

void TimelineTool::RequestAction(const EditorToolContext& context, PendingAction action) {

	session_.CommitHistory();
	if (HasPendingEdits()) { pending_ = std::move(action); return; }
	ExecuteAction(context, action);
}

bool TimelineTool::ExecuteAction(const EditorToolContext& context, const PendingAction& action) {

	recording_.EndRecording();
	bool success = false;
	switch (action.kind) {
	case PendingKind::Load: success = session_.Load(context, action.asset); break;
	case PendingKind::Create: success = session_.Create(context, action.name); break;
	case PendingKind::Import: success = session_.Import(context, action.asset); break;
	case PendingKind::Close:
		ResetInteraction();
		session_.EndPreview();
		if (!resolveClose_) openWindow_ = false;
		return true;
	}
	if (success) {
		ResetInteraction();
		selectedTrack_ = {};
		selectedClips_.clear();
		clipboard_.clear();
		collapsedGroups_.clear();
		expandedCurves_.clear();
		viewStart_ = 0.0;
		if (action.assetOnly) { session_.SetOwner(context, {}); queuedOwner_ = {}; SelectInspection(context); } else if (queuedOwner_) { session_.SetOwner(context, queuedOwner_); queuedOwner_ = {}; }
		session_.rangeStart = 0.0;
		session_.rangeEnd = session_.GetAsset().duration;
	}
	return success;
}

void TimelineTool::DrawPending(const EditorToolContext& context) {

	if (!pending_) return;
	ImGui::OpenPopup("タイムラインの未保存編集");
	if (!MyGUI::BeginPopupModal("タイムラインの未保存編集", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
	MyGUI::Text("変更を保存しますか？");
	const auto finish = [&] {
		const auto action = *pending_;
		if (!ExecuteAction(context, action)) return;
		pending_.reset();
		if (resolveClose_) closeResult_ = EditorToolCloseResult::Accepted;
		resolveClose_ = false;
		ImGui::CloseCurrentPopup();
		};
	if (ImGui::Button("保存", ImVec2(110, 0)) && SaveAll(context)) finish();
	ImGui::SameLine();
	if (ImGui::Button("破棄", ImVec2(110, 0))) {
		DiscardAll(context);
		finish();
	}
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(110, 0))) {
		pending_.reset();
		queuedOwner_ = {};
		if (resolveClose_) closeResult_ = EditorToolCloseResult::Cancelled;
		resolveClose_ = false;
		ImGui::CloseCurrentPopup();
	}
	if (!session_.GetError().empty()) MyGUI::Text("診断", session_.GetError());
	ImGui::EndPopup();
}
