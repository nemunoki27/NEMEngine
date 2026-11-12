#include "TimelineTool.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Core/Animation/Timeline/TimelineBuiltinConfig.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <optional>
// imgui
#include <imgui_internal.h>

using namespace Engine;

void TimelineTool::DrawAssetDrop(const EditorToolContext& context, ImVec2 origin, float width, float height) {

	const ImVec2 window = ImGui::GetWindowPos();
	const float timeX = origin.x + labelWidth_;
	const ImRect area(ImVec2(origin.x, window.y), ImVec2(origin.x + width, (std::min)(window.y + ImGui::GetWindowSize().y, origin.y + height)));
	if (ImGui::BeginDragDropTargetCustom(area, ImGui::GetID("TimelineAssetDrop"))) {
		if (const auto* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kProjectAssetDragDropPayloadType);
			payload && payload->DataSize == sizeof(EditorAssetDragDropPayload)) {
			const auto& asset = *static_cast<const EditorAssetDragDropPayload*>(payload->Data);
			std::optional<TimelineTrackKind> kind;
			if (asset.assetType == AssetType::Prefab) kind = TimelineTrackKind::Prefab;
			if (asset.assetType == AssetType::Audio) kind = TimelineTrackKind::Audio;
			if (asset.assetType == AssetType::AnimationClip) kind = TimelineTrackKind::Property;
			if (asset.assetType == AssetType::Timeline && asset.assetID != session_.GetAssetID()) kind = TimelineTrackKind::SubTimeline;
			if (kind) {
				TimelineClip clip;
				clip.id = UUID::New();
				clip.start = (std::max)(0.0, viewStart_ + (ImGui::GetIO().MousePos.x - timeX) / pixelsPerSecond_);
				if (snap_ && !ImGui::GetIO().KeyShift)
					clip.start = std::round(clip.start * session_.GetAsset().frameRate) / session_.GetAsset().frameRate;
				clip.data = CreateDefaultTimelineClipData(*kind);
				clip.data[*kind == TimelineTrackKind::SubTimeline ? "timeline" : "asset"] = ToAssetReferenceJson(asset.assetID);
				clip.name = TimelineEditorFields::TrackLabel(*kind);
				if (const auto* database = context.toolContext.assetDatabase) {
					if (*kind == TimelineTrackKind::Property) {
						AnimationClipAsset animation;
						if (LoadAnimationClipAsset(database->ResolveFullPath(asset.assetID), animation)) clip.duration = (std::max)(0.001, static_cast<double>(animation.duration));
					} else if (*kind == TimelineTrackKind::SubTimeline) {
						TimelineAsset child;
						if (LoadTimelineAsset(database->ResolveFullPath(asset.assetID), child)) clip.duration = child.duration;
					}
					if (const auto* meta = database->Find(asset.assetID)) clip.name = std::filesystem::path(meta->assetPath).filename().string();
				}
				clip.sourceDuration = clip.duration;
				TimelineTrack* destination = nullptr;
				for (const auto row : visibleTracks_) if (ImGui::GetIO().MousePos.y >= origin.y + row.y &&
					ImGui::GetIO().MousePos.y < origin.y + row.y + row.height) { destination = FindTrack(row.id); break; }
				if ((destination && destination->locked) || (editMode_ == TimelineEditMode::Replace && !TimelineEditOperations::CanSplit(*kind, clip))) {
					ImGui::EndDragDropTarget();
					return;
				}
				const TimelineAsset before = session_.GetAsset();
				if (!destination || destination->kind != *kind) {
					const UUID parent = destination && destination->kind == TimelineTrackKind::Group ? destination->id : UUID{};
					AddTrack(*kind, parent);
					destination = FindTrack(selectedTrack_);
				}
				const UUID trackID = destination->id, clipID = clip.id;
				if (TimelineEditOperations::Insert(session_.GetAsset(), trackID, std::move(clip), editMode_, autoBlend_)) {
					selectedTrack_ = trackID;
					selectedClips_ = {{trackID, clipID}};
					session_.MarkChanged();
					SelectInspection(context, trackID, clipID);
				} else { session_.GetAsset() = before; NormalizeSelection(); }
			}
		}
		if (const auto* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType);
			payload && payload->DataSize == sizeof(UUID) && context.GetWorld()) {
			droppedEntity_ = *static_cast<const UUID*>(payload->Data);
			droppedTime_ = (std::max)(0.0, viewStart_ + (ImGui::GetIO().MousePos.x - timeX) / pixelsPerSecond_);
			if (snap_) droppedTime_ = std::round(droppedTime_ * session_.GetAsset().frameRate) / session_.GetAsset().frameRate;
			ImGui::OpenPopup("TimelineEntityDrop");
		}
		ImGui::EndDragDropTarget();
	}
	if (ImGui::BeginPopup("TimelineEntityDrop")) {
		ECSWorld* world = context.GetWorld();
		const Entity entity = world ? world->FindByUUID(droppedEntity_) : Entity::Null();
		if (world && world->IsAlive(entity)) {
			for (const auto kind : {TimelineTrackKind::SkinnedAnimation, TimelineTrackKind::Motion, TimelineTrackKind::Particle,
				TimelineTrackKind::Collision, TimelineTrackKind::Camera, TimelineTrackKind::Audio, TimelineTrackKind::Property,
				TimelineTrackKind::Activation, TimelineTrackKind::TimeScale}) {
				if (!HasTimelineBindingComponent(*world, entity, TimelineEditorFields::RequiredComponent(kind))) continue;
				if (ImGui::MenuItem(TimelineEditorFields::TrackLabel(kind))) {
					const double previous = session_.time;
					session_.time = droppedTime_;
					AddTrack(kind);
					SetTrackBinding(context, selectedTrack_, droppedEntity_, true);
					session_.time = previous;
					SelectInspection(context, selectedTrack_);
				}
			}
		}
		ImGui::EndPopup();
	}
}
