#include "TimelineTool.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Editor/UI/Inspectors/Common/TimelinePlayerFields.h>

// c++
#include <algorithm>

using namespace Engine;

void TimelineTool::DrawTrackBinding(const EditorToolContext& context, TimelineTrack& track, ImVec2 origin, float width) {

	ECSWorld* world = context.GetWorld();
	const Entity target = ResolveTrackTarget(context, track);
	UUID value = world && world->IsAlive(target) ? world->GetUUID(target) : UUID{};
	const bool shared = std::count_if(session_.GetAsset().tracks.begin(), session_.GetAsset().tracks.end(),
		[&](const auto& other) { return other.binding == track.binding; }) > 1;
	EntityEditSetting setting;
	setting.useAutoPropertyRow = false;
	setting.buttonSize = ImVec2(width, 17.0f);
	setting.acceptEntity = [&](ECSWorld& source, Entity entity) {
		return shared ? HasTimelineBindingComponent(source, entity, TimelineEditorFields::RequiredComponent(track.kind)) :
			IsTimelineBindingTargetAllowed(source, session_.GetAsset(), track.binding, entity);
	};
	setting.rejectReason = "トラックに必要なコンポーネントを持つオブジェクトを設定してください";
	ImGui::SetCursorScreenPos(origin);
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 0.0f));
	ImGui::BeginDisabled(track.locked || !world || !world->IsAlive(session_.GetOwner(context)));
	if (MyGUI::EntityReferenceField("##Binding", value, world, setting).valueChanged) {
		if (shared) { pendingBindingTrack_ = track.id; pendingBindingTarget_ = value; }
		else SetTrackBinding(context, track.id, value, true);
	}
	ImGui::EndDisabled();
	ImGui::PopStyleVar();
}

void TimelineTool::SetTrackBinding(const EditorToolContext& context, UUID trackID, UUID targetID, bool shared) {

	auto* track = FindTrack(trackID);
	ECSWorld* world = context.GetWorld();
	if (!track || !world || track->locked) return;
	auto& asset = session_.GetAsset();
	const Entity target = world->FindByUUID(targetID);
	if (targetID && !(shared ? IsTimelineBindingTargetAllowed(*world, asset, track->binding, target) :
		HasTimelineBindingComponent(*world, target, TimelineEditorFields::RequiredComponent(track->kind)))) return;
	if (!shared) {
		const auto original = std::find_if(asset.bindings.begin(), asset.bindings.end(), [&](const auto& value) { return value.id == track->binding; });
		TimelineBindingDefinition binding = original != asset.bindings.end() ? *original : TimelineBindingDefinition{};
		binding.id = UUID::New();
		binding.component = TimelineEditorFields::RequiredComponent(track->kind);
		const std::string base = binding.name.empty() ? "Actor" : binding.name;
		for (uint32_t suffix = 2;; ++suffix) {
			binding.name = base + std::to_string(suffix);
			if (std::none_of(asset.bindings.begin(), asset.bindings.end(), [&](const auto& value) { return value.name == binding.name; })) break;
		}
		asset.bindings.push_back(std::move(binding));
		track->binding = asset.bindings.back().id;
		session_.MarkChanged();
	}
	auto& bindings = session_.GetPreviewSettings().bindings;
	auto found = std::find_if(bindings.begin(), bindings.end(), [&](const auto& value) { return value.slot == track->binding; });
	if (found == bindings.end()) { bindings.push_back({track->binding}); found = std::prev(bindings.end()); }
	found->target = targetID ? SceneEntityReference::FromEntity(*world, target) : SceneEntityReference{};
	found->runtimeTargetID = found->target.localFileID ? UUID{} : targetID;
	session_.SaveBindings(context);
}

void TimelineTool::DrawBindingChoice(const EditorToolContext& context) {

	if (pendingBindingTrack_) ImGui::OpenPopup("共有している割り当ての変更");
	const float buttonWidth = (std::max)(160.0f, ImGui::CalcTextSize("このトラックのみ").x + ImGui::GetStyle().FramePadding.x * 2.0f);
	const float popupWidth = buttonWidth * 3.0f + ImGui::GetStyle().ItemSpacing.x * 2.0f + ImGui::GetStyle().WindowPadding.x * 2.0f;
	if (!MyGUI::BeginPopupModal("共有している割り当ての変更", nullptr, ImGuiWindowFlags_AlwaysAutoResize, popupWidth)) return;
	MyGUI::Text("この割り当ては複数のトラックで使われています");
	if (ImGui::Button("このトラックのみ", ImVec2(buttonWidth, 0))) {
		SetTrackBinding(context, pendingBindingTrack_, pendingBindingTarget_, false);
		pendingBindingTrack_ = {}; ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	const auto* track = FindTrack(pendingBindingTrack_);
	const bool valid = track && context.GetWorld() && (!pendingBindingTarget_ ||
		IsTimelineBindingTargetAllowed(*context.GetWorld(), session_.GetAsset(), track->binding,
			context.GetWorld()->FindByUUID(pendingBindingTarget_)));
	ImGui::BeginDisabled(!valid);
	if (ImGui::Button("共有先すべて", ImVec2(buttonWidth, 0))) {
		SetTrackBinding(context, pendingBindingTrack_, pendingBindingTarget_, true);
		pendingBindingTrack_ = {}; ImGui::CloseCurrentPopup();
	}
	ImGui::EndDisabled();
	if (!valid && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
		ImGui::SetTooltip("共有する他のトラックに必要なコンポーネントがありません");
	ImGui::SameLine();
	if (ImGui::Button("キャンセル", ImVec2(buttonWidth, 0))) { pendingBindingTrack_ = {}; ImGui::CloseCurrentPopup(); }
	ImGui::EndPopup();
}
