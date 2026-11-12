#include "TimelineTool.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/Tools/Core/EditorToolUI.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

using namespace Engine;

void TimelineTool::ToggleTrackRecording(const EditorToolContext& context, UUID trackID) {

	auto* track = FindTrack(trackID);
	if (!track || track->locked || track->kind != TimelineTrackKind::Property || context.IsPlaying()) return;
	session_.playing = false;
	UUID clipID{};
	for (const auto& selection : selectedClips_) if (selection.track == trackID) { clipID = selection.clip; break; }
	const Entity target = ResolveTrackTarget(context, *track);
	EditorToolUI::EndOtherScenePreviews(descriptor_.id);
	recording_.ToggleRecording(context, session_.GetAsset(), trackID, clipID, target, session_.time);
	if (recording_.ConsumeAssetChanged()) session_.MarkChanged();
	if (!recording_.IsRecording()) return;
	session_.previewEnabled = true;
	targetLocked_ = true;
	if (context.panelContext && context.panelContext->editorState) context.panelContext->editorState->SelectEntity(target);
}

bool TimelineTool::IsTrackRecording(UUID trackID) const {

	return recording_.IsRecording(trackID);
}

bool TimelineTool::DrawTrackCurves(const EditorToolContext& context, UUID trackID, ImVec2 size) {

	auto* track = FindTrack(trackID);
	if (!track) return false;
	UUID clipID{};
	for (const auto& selection : selectedClips_) if (selection.track == trackID) { clipID = selection.clip; break; }
	const double previous = session_.time;
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	const bool result = recording_.DrawCurves(context, session_.GetAsset(), trackID, clipID,
		ResolveTrackTarget(context, *track), session_.time, size);
	if (!result) {
		MyGUI::Text("アニメーションクリップを設定するか、記録を開始してください");
		ImGui::BeginDisabled(context.IsPlaying() || track->locked || !context.GetWorld() ||
			!context.GetWorld()->IsAlive(ResolveTrackTarget(context, *track)));
		if (ImGui::Button("記録用クリップを作成")) ToggleTrackRecording(context, trackID);
		ImGui::EndDisabled();
	}
	const auto mouse = ImGui::GetIO().MousePos;
	if (result && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && mouse.x >= origin.x && mouse.y >= origin.y &&
		mouse.x < origin.x + size.x && mouse.y < origin.y + size.y)
		SelectInspection(context, trackID, recording_.GetCurveClipID());
	if (previous != session_.time) session_.Seek(context, session_.time);
	return result;
}

bool TimelineTool::SaveAll(const EditorToolContext& context) {

	if (!recording_.Save(context)) return false;
	return !session_.HasAsset() || session_.Save(context);
}

void TimelineTool::DiscardAll(const EditorToolContext& context) {

	recording_.EndRecording();
	recording_.Discard();
	session_.DiscardChanges(context);
	session_.PublishDraft(context);
}
