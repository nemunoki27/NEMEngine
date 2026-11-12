#include "TimelineTool.h"
#include "TimelineEditorFields.h"
#include "TimelineMultiClipInspector.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Inspectors/Common/TimelinePlayerFields.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>
#include <exception>
#include <iterator>
#include <stdexcept>

using namespace Engine;

bool TimelineTool::IsInspectionTargetValid(const EditorInspectionTarget& target) const {

	if (!openWindow_ || target.providerID != descriptor_.id || !session_.HasAsset() || target.asset != session_.GetAssetID()) return false;
	if (!target.primary) return !target.secondary;
	const auto& tracks = session_.GetAsset().tracks;
	const auto track = std::find_if(tracks.begin(), tracks.end(), [&](const auto& value) { return value.id == target.primary; });
	return track != tracks.end() && (!target.secondary || std::any_of(track->clips.begin(), track->clips.end(),
		[&](const auto& clip) { return clip.id == target.secondary; }));
}

bool TimelineTool::DrawSelectionInspector(const EditorPanelContext& panel, const EditorInspectionTarget& target) {

	if (!IsInspectionTargetValid(target)) return false;
	const EditorToolContext context = MakeEditorToolContext(panel);
	const auto& io = ImGui::GetIO();
	const bool keyboard = std::any_of(std::begin(io.KeysData), std::end(io.KeysData), [](const auto& key) { return key.Down; });
	const bool capture = inspectedTarget_ != target || ImGui::IsAnyItemActive() || ImGui::IsAnyMouseDown() || keyboard;
	inspectedTarget_ = target;
	nlohmann::json before, bindings;
	if (capture) { before = session_.GetAsset(); bindings = session_.GetPreviewSettings(); }
	if (!target.primary) DrawInspector(context);
	else if (auto* track = FindTrack(target.primary)) {
		if (target.secondary) {
			const bool keySelection = recording_.GetCurveTrackID() == target.primary &&
				recording_.GetCurveClipID() == target.secondary && recording_.HasSelectedKeys();
			if (keySelection) recording_.DrawKeyInspector(context);
			else if (selectedClips_.size() > 1 && IsSelected(target.primary, target.secondary)) {
				std::vector<TimelineClipSelection> selection;
				for (const auto& item : selectedClips_) selection.push_back({item.track, item.clip});
				DrawTimelineMultiClipInspector(session_.GetAsset(), selection, editMode_, autoBlend_, frames_);
			}
			else if (auto* clip = FindClip(target.primary, target.secondary)) DrawClipInspector(context, *track, *clip);
		} else {
			DrawTrackInspector(*track);
			track = FindTrack(target.primary);
			if (track && track->binding && context.GetWorld() && context.GetWorld()->IsAlive(session_.GetOwner(context))) {
				ImGui::Separator();
				if (DrawTimelineBindingField("再生対象", *context.GetWorld(), session_.GetOwner(context),
					session_.GetPreviewSettings(), session_.GetAsset(), track->binding).valueChanged) session_.SaveBindings(context);
			}
		}
	}
	if (capture && before != nlohmann::json(session_.GetAsset())) {
		UpdateTimelineAssetDuration(session_.GetAsset());
		session_.MarkChanged();
	}
	if (capture && bindings != nlohmann::json(session_.GetPreviewSettings())) session_.MarkChanged();
	if (!ImGui::IsAnyItemActive() && dragMode_ == DragMode::None) session_.CommitHistory();
	session_.PublishDraft(context);
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput) {
		if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S)) SaveAll(context);
		if (!ImGui::IsAnyItemActive()) {
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z)) session_.RestoreHistory(false);
			if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y)) session_.RestoreHistory(true);
		}
	}
	return true;
}

void TimelineTool::DrawInspector(const EditorToolContext& context) {

	auto& asset = session_.GetAsset();
	MyGUI::Text("種類", "Timeline");
	MyGUI::InputText("名前", asset.name);
	const char* durationModes[]{"固定長", "クリップの末尾に追従"};
	int mode = static_cast<int>(asset.durationMode);
	if (TimelineEditorFields::Select("長さの設定", mode, durationModes, 2).valueChanged)
		asset.durationMode = static_cast<TimelineDurationMode>(mode);
	if (asset.durationMode == TimelineDurationMode::FixedLength)
		TimelineEditorFields::Time("長さ", asset.duration, asset.frameRate, frames_, 0.001);
	else MyGUI::TextDouble("長さ", asset.duration);
	float fps = static_cast<float>(asset.frameRate);
	if (MyGUI::DragFloat("基準FPS", fps, {.dragSpeed = 1.0f, .minValue = 1.0f, .maxValue = 1000.0f}).valueChanged && std::isfinite(fps))
		asset.frameRate = (std::clamp)(fps, 1.0f, 1000.0f);
	ImGui::Separator();
	DrawBindings(context);
	DrawParameters();
}

void TimelineTool::DrawTrackInspector(TimelineTrack& track) {

	MyGUI::Text("種類", TimelineEditorFields::TrackLabel(track.kind));
	MyGUI::InputText("名前", track.name);
	MyGUI::CheckboxField("ミュート", track.muted);
	MyGUI::CheckboxField("ロック", track.locked);
	ImGui::BeginDisabled(track.locked);
	if (track.kind != TimelineTrackKind::Group) TimelineEditorFields::Binding("割り当て", track.binding, session_.GetAsset());
	std::vector<UUID> groups{{}};
	std::vector<std::string> labels{"なし"};
	int selected = 0;
	for (const auto& group : session_.GetAsset().tracks) {
		if (group.kind != TimelineTrackKind::Group || group.id == track.id) continue;
		UUID parent = group.parent;
		bool child = false;
		for (int depth = 0; parent && depth < 64; ++depth) {
			if (parent == track.id) { child = true; break; }
			const auto* next = FindTrack(parent);
			parent = next ? next->parent : UUID{};
		}
		if (child) continue;
		groups.push_back(group.id);
		labels.push_back(group.name);
		if (group.id == track.parent) selected = static_cast<int>(groups.size() - 1);
	}
	std::vector<const char*> names;
	for (const auto& name : labels) names.push_back(name.c_str());
	if (TimelineEditorFields::Select("親グループ", selected, names.data(), static_cast<int>(names.size())).valueChanged)
		track.parent = groups[selected];
	auto& tracks = session_.GetAsset().tracks;
	const size_t index = static_cast<size_t>(&track - tracks.data());
	const float width = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	const bool up = ImGui::Button("上へ", ImVec2(width, 0));
	ImGui::SameLine();
	const bool down = ImGui::Button("下へ", ImVec2(width, 0));
	if (up || down) {
		for (ptrdiff_t next = static_cast<ptrdiff_t>(index) + (up ? -1 : 1);
			next >= 0 && next < static_cast<ptrdiff_t>(tracks.size()); next += up ? -1 : 1) {
			if (tracks[next].parent != track.parent) continue;
			std::swap(tracks[index], tracks[next]);
			break;
		}
		ImGui::EndDisabled();
		return;
	}
	if (track.kind == TimelineTrackKind::Group) {
		TimelineEditorFields::Time("開始", track.start, session_.GetAsset().frameRate, frames_);
		TimelineEditorFields::Time("区間・0は全体", track.duration, session_.GetAsset().frameRate, frames_);
		MyGUI::CheckboxField("それ以外の分岐", track.fallback);
		std::vector<UUID> branches{{}};
		std::vector<std::string> branchLabels{"独立した条件"};
		selected = 0;
		for (const auto& group : session_.GetAsset().tracks) {
			if (group.kind != TimelineTrackKind::Group || group.parent != track.parent || !group.branchGroup) continue;
			if (std::find(branches.begin(), branches.end(), group.branchGroup) != branches.end()) continue;
			branches.push_back(group.branchGroup);
			branchLabels.push_back(group.name);
			if (track.branchGroup == group.branchGroup) selected = static_cast<int>(branches.size() - 1);
		}
		names.clear();
		for (const auto& label : branchLabels) names.push_back(label.c_str());
		if (TimelineEditorFields::Select("分岐グループ", selected, names.data(), static_cast<int>(names.size())).valueChanged)
			track.branchGroup = branches[selected];
		if (ImGui::Button("新しい分岐グループにする", ImVec2(-1, 0))) track.branchGroup = UUID::New();
		if (track.branchGroup && ImGui::Button("それ以外の分岐を追加", ImVec2(-1, 0))) {
			TimelineTrack other;
			other.id = UUID::New(); other.name = "それ以外"; other.kind = TimelineTrackKind::Group;
			other.parent = track.parent; other.branchGroup = track.branchGroup; other.fallback = true;
			other.start = track.start; other.duration = track.duration;
			session_.GetAsset().tracks.push_back(std::move(other));
			ImGui::EndDisabled();
			return;
		}
		DrawConditions(track);
	}
	ImGui::EndDisabled();
}

void TimelineTool::DrawClipInspector(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip) {

	MyGUI::Text("クリップ", TimelineEditorFields::TrackLabel(track.kind));
	ImGui::BeginDisabled(track.locked);
	MyGUI::CheckboxField("有効", clip.enabled);
	MyGUI::InputText("名前", clip.name);
	const double fps = session_.GetAsset().frameRate;
	double start = clip.start;
	double duration = clip.duration;
	const auto startEdit = TimelineEditorFields::Time("開始", start, fps, frames_);
	const auto durationEdit = TimelineEditorFields::Time("長さ", duration, fps, frames_, frames_ ? 1.0 / fps : 0.001);
	if (startEdit.valueChanged || durationEdit.valueChanged) {
		const UUID trackID = track.id, clipID = clip.id;
		ApplyClipTiming(trackID, clipID, start, duration, clip.clipIn);
		ImGui::EndDisabled();
		return;
	}
	const bool sourceTime = track.kind == TimelineTrackKind::SkinnedAnimation || track.kind == TimelineTrackKind::Motion ||
		track.kind == TimelineTrackKind::Particle || track.kind == TimelineTrackKind::Camera || track.kind == TimelineTrackKind::Audio ||
		track.kind == TimelineTrackKind::Property || track.kind == TimelineTrackKind::SubTimeline || track.kind == TimelineTrackKind::Custom;
	if (sourceTime) {
		TimelineEditorFields::Time("素材の開始位置", clip.clipIn, fps, frames_);
		float speed = static_cast<float>(clip.speed);
		if (MyGUI::DragFloat("素材の再生速度", speed, {.dragSpeed = 0.01f, .minValue = 0.0f, .maxValue = 100.0f}).valueChanged)
			if (std::isfinite(speed)) clip.speed = (std::clamp)(speed, 0.0f, 100.0f);
	}
	if (TimelineEditOperations::SupportsBlend(track.kind)) {
		const auto begin = TimelineEditorFields::Time("ブレンド開始", clip.blendIn, fps, frames_);
		const auto end = TimelineEditorFields::Time("ブレンド終了", clip.blendOut, fps, frames_);
		if (begin.valueChanged || end.valueChanged) autoBlend_ = false;
		clip.blendIn = (std::min)(clip.blendIn, clip.duration);
		clip.blendOut = (std::min)(clip.blendOut, clip.duration);
	}
	if (track.kind == TimelineTrackKind::Motion || track.kind == TimelineTrackKind::Property) MyGUI::EnumCombo("イージング", clip.easing);
	ImGui::Separator();
	try {
		if (!ValidateClipData(track, clip)) throw std::invalid_argument(clipDataError_);
		DrawClipData(context, track, clip);
	}
	catch (const std::exception&) {
		MyGUI::Text("設定", "クリップの設定形式が正しくありません");
		if (!clipDataError_.empty()) MyGUI::Text("詳細", clipDataError_);
		if (ImGui::Button("このクリップの設定を初期化")) {
			clip.data = CreateDefaultTimelineClipData(track.kind);
		}
	}
	ImGui::EndDisabled();
}
