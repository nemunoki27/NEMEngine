#include "TimelineTool.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <functional>
#include <unordered_map>

using namespace Engine;

TimelineTrack* TimelineTool::FindTrack(UUID id) {

	auto& tracks = session_.GetAsset().tracks;
	const auto found = std::find_if(tracks.begin(), tracks.end(), [&](const auto& value) { return value.id == id; });
	return found == tracks.end() ? nullptr : &*found;
}

TimelineClip* TimelineTool::FindClip(UUID track, UUID clip) {

	if (auto* source = FindTrack(track)) {
		const auto found = std::find_if(source->clips.begin(), source->clips.end(), [&](const auto& value) { return value.id == clip; });
		if (found != source->clips.end()) return &*found;
	}
	return nullptr;
}

bool TimelineTool::IsSelected(UUID track, UUID clip) const {

	return std::any_of(selectedClips_.begin(), selectedClips_.end(),
		[&](const auto& selected) { return selected.track == track && selected.clip == clip; });
}

void TimelineTool::NormalizeSelection() {

	if (!FindTrack(selectedTrack_)) selectedTrack_ = {};
	std::erase_if(selectedClips_, [&](const auto& selected) { return !FindClip(selected.track, selected.clip); });
}

Entity TimelineTool::ResolveTrackTarget(const EditorToolContext& context, const TimelineTrack& track) const {

	ECSWorld* world = context.GetWorld();
	if (!world) return Entity::Null();
	return ResolveTimelineBinding(*world, session_.GetOwner(context), session_.GetPreviewSettings(), track.binding);
}

void TimelineTool::AddTrack(TimelineTrackKind kind, UUID parent) {

	auto& asset = session_.GetAsset();
	TimelineTrack track;
	track.id = UUID::New();
	track.kind = kind;
	track.name = TimelineEditorFields::TrackLabel(kind);
	track.parent = parent;
	const std::string required = TimelineEditorFields::RequiredComponent(kind);
	if (kind != TimelineTrackKind::Group) {
		const char* role = kind == TimelineTrackKind::Camera ? "Camera" : kind == TimelineTrackKind::Audio ? "Audio" :
			kind == TimelineTrackKind::Collision ? "Hitbox" : kind == TimelineTrackKind::Particle ? "Effect" : "Actor";
		std::string name = role;
		for (uint32_t suffix = 2; std::any_of(asset.bindings.begin(), asset.bindings.end(),
			[&](const auto& value) { return value.name == name; }); ++suffix) name = role + std::to_string(suffix);
		asset.bindings.push_back({UUID::New(), name, required});
		track.binding = asset.bindings.back().id;
	}
	asset.tracks.push_back(std::move(track));
	selectedTrack_ = asset.tracks.back().id;
	selectedClips_.clear();
	session_.MarkChanged();
}

bool TimelineTool::AddClip(TimelineTrack& track) {

	if (track.locked || track.kind == TimelineTrackKind::Group) return false;
	TimelineClip clip;
	clip.id = UUID::New();
	clip.name = TimelineEditorFields::TrackLabel(track.kind);
	clip.start = session_.time;
	clip.duration = track.kind == TimelineTrackKind::Event ? 1.0 / session_.GetAsset().frameRate : 1.0;
	clip.sourceDuration = clip.duration;
	clip.data = CreateDefaultTimelineClipData(track.kind);
	if (track.kind == TimelineTrackKind::Event) clip.data["name"] = "Event";
	if (track.kind == TimelineTrackKind::Window) clip.data["name"] = "Combo";
	if (track.kind == TimelineTrackKind::SubTimeline) clip.data = {{"timeline", ""}, {"loop", false}, {"bindings", nlohmann::json::object()}};
	if (track.kind == TimelineTrackKind::Custom) clip.data = {{"typeName", ""}, {"fields", nlohmann::json::object()}};
	const UUID trackID = track.id, clipID = clip.id;
	if (!TimelineEditOperations::Insert(session_.GetAsset(), trackID, std::move(clip), editMode_, autoBlend_)) return false;
	selectedTrack_ = trackID;
	selectedClips_ = {{trackID, clipID}};
	session_.MarkChanged();
	return true;
}

void TimelineTool::RemoveTrack(UUID id) {

	auto& tracks = session_.GetAsset().tracks;
	std::unordered_set<UUID> removed{id};
	bool added = true;
	while (added) {
		added = false;
		for (const auto& track : tracks) if (removed.contains(track.parent) && removed.insert(track.id).second) added = true;
	}
	if (std::any_of(tracks.begin(), tracks.end(), [&](const auto& track) { return removed.contains(track.id) && track.locked; })) return;
	std::erase_if(tracks, [&](const auto& track) { return removed.contains(track.id); });
	TimelineEditOperations::UpdateDuration(session_.GetAsset());
	session_.MarkChanged();
	NormalizeSelection();
}

void TimelineTool::DeleteSelection() {

	if (!selectedClips_.empty()) {
		for (const auto& selected : selectedClips_) if (auto* track = FindTrack(selected.track); track && !track->locked)
			std::erase_if(track->clips, [&](const auto& clip) { return clip.id == selected.clip; });
		selectedClips_.clear();
		TimelineEditOperations::UpdateDuration(session_.GetAsset());
		session_.MarkChanged();
	} else if (auto* track = FindTrack(selectedTrack_); track && !track->locked) RemoveTrack(track->id);
}

void TimelineTool::DuplicateTrack(UUID id) {

	auto& tracks = session_.GetAsset().tracks;
	std::unordered_map<UUID, UUID> identities{{id, UUID::New()}};
	for (size_t count = 0; count < tracks.size(); ++count) {
		bool added = false;
		for (const auto& track : tracks) {
			if (identities.contains(track.parent) && !identities.contains(track.id)) {
				identities.emplace(track.id, UUID::New());
				added = true;
			}
		}
		if (!added) break;
	}
	std::unordered_map<UUID, UUID> branches;
	std::vector<TimelineTrack> copies;
	for (const auto& track : tracks) {
		if (!identities.contains(track.id)) continue;
		auto copy = track;
		copy.id = identities.at(track.id);
		if (track.id == id) copy.name += " コピー";
		if (identities.contains(track.parent)) copy.parent = identities.at(track.parent);
		if (copy.branchGroup && track.id != id) {
			if (!branches.contains(copy.branchGroup)) branches.emplace(copy.branchGroup, UUID::New());
			copy.branchGroup = branches.at(copy.branchGroup);
		}
		for (auto& clip : copy.clips) clip.id = UUID::New();
		copies.push_back(std::move(copy));
	}
	for (auto& copy : copies) tracks.push_back(std::move(copy));
	selectedTrack_ = identities.at(id);
	selectedClips_.clear();
	session_.MarkChanged();
}

void TimelineTool::CopySelection() {

	clipboard_.clear();
	for (const auto& selection : selectedClips_) {
		const auto* track = FindTrack(selection.track);
		const auto* clip = FindClip(selection.track, selection.clip);
		if (track && clip) clipboard_.push_back({track->id, track->binding, track->parent, track->kind, *clip});
	}
}

void TimelineTool::PasteSelection() {

	if (clipboard_.empty()) return;
	double firstTime = clipboard_.front().clip.start;
	for (const auto& value : clipboard_) firstTime = (std::min)(firstTime, value.clip.start);
	const bool singleTrack = std::all_of(clipboard_.begin(), clipboard_.end(),
		[&](const auto& value) { return value.track == clipboard_.front().track; });
	std::unordered_map<UUID, UUID> destinations;
	std::vector<ClipSelection> next;
	for (const auto& copied : clipboard_) {
		const auto& source = copied.clip;
		auto* track = FindTrack(singleTrack ? selectedTrack_ : copied.track);
		if (destinations.contains(copied.track)) track = FindTrack(destinations.at(copied.track));
		if (!track || track->kind != copied.kind || track->locked) {
			AddTrack(copied.kind, FindTrack(copied.parent) ? copied.parent : UUID{});
			track = FindTrack(selectedTrack_);
			track->clips.clear();
			track->binding = copied.binding;
		}
		destinations[copied.track] = track->id;
		const UUID destinationID = track->id;
		auto clip = source;
		clip.id = UUID::New();
		clip.start = session_.time + source.start - firstTime;
		const UUID clipID = clip.id;
		if (TimelineEditOperations::Insert(session_.GetAsset(), destinationID, std::move(clip), editMode_, autoBlend_))
			next.push_back({destinationID, clipID});
	}
	selectedClips_ = std::move(next);
	TimelineEditOperations::UpdateDuration(session_.GetAsset());
	session_.MarkChanged();
}

void TimelineTool::DuplicateSelection() {

	CopySelection();
	if (clipboard_.empty()) return;
	const double previous = session_.time;
	double end = 0.0;
	for (const auto& entry : clipboard_) end = (std::max)(end, entry.clip.start + entry.clip.duration);
	session_.time = end;
	PasteSelection();
	session_.time = previous;
}

void TimelineTool::RebuildVisibleTracks() {

	visibleTracks_.clear();
	float y = 0.0f;
	const float scale = ImGui::GetFontSize() / 15.0f;
	std::function<void(UUID, int)> visit = [&](UUID parent, int depth) {
		if (depth > 64) return;
		for (const auto& track : session_.GetAsset().tracks) {
			if (track.parent != parent || (!showEvents_ && track.kind == TimelineTrackKind::Event)) continue;
			const float height = (rowHeight_ + (expandedCurves_.contains(track.id) ? 160.0f : 0.0f)) * scale;
			visibleTracks_.push_back({track.id, depth, y, height});
			y += height;
			if (!collapsedGroups_.contains(track.id)) visit(track.id, depth + 1);
		}
	};
	visit({}, 0);
}

void TimelineTool::DrawAddTrackMenu(const EditorToolContext& context) {

	const auto add = [&](TimelineTrackKind kind) {
		if (ImGui::MenuItem(TimelineEditorFields::TrackLabel(kind))) {
			const auto* selected = FindTrack(selectedTrack_);
			const UUID parent = selected ? (selected->kind == TimelineTrackKind::Group ? selected->id : selected->parent) : UUID{};
			AddTrack(kind, parent);
			SelectInspection(context, selectedTrack_);
		}
	};
	if (ImGui::BeginMenu("アニメーション")) {
		add(TimelineTrackKind::SkinnedAnimation); add(TimelineTrackKind::Motion); add(TimelineTrackKind::Property);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("発生・状態")) {
		add(TimelineTrackKind::Particle); add(TimelineTrackKind::Prefab);
		add(TimelineTrackKind::Collision); add(TimelineTrackKind::Activation);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("演出")) {
		add(TimelineTrackKind::Camera); add(TimelineTrackKind::Audio); add(TimelineTrackKind::TimeScale);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("制御")) {
		add(TimelineTrackKind::Event); add(TimelineTrackKind::Window);
		add(TimelineTrackKind::SubTimeline); add(TimelineTrackKind::Custom);
		ImGui::EndMenu();
	}
	ImGui::Separator();
	add(TimelineTrackKind::Group);
}
