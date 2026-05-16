#include "AnimationClipTool.h"

using namespace Engine;

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/NameComponent.h>
#include <Engine/Core/Animation/Clip/AnimationClipAsset.h>
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/Utility/ImGui/MyGUI.h>
#include <Engine/Core/Logger/Logger.h>

// imgui
#include <imgui.h>
// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <unordered_map>

//============================================================================
//	AnimationClipTool classMethods
//============================================================================

namespace {

	constexpr std::array<const char*, 3> kApplyModeNames{ "Override", "Add", "Multiply" };

	std::string BuildTrackLabel(const AnimationCurveTrack& track) {

		return track.binding.componentName + "." + track.binding.propertyPath;
	}

	std::string BuildTargetEntityLabel(ECSWorld* world, const Entity& entity, Engine::UUID uuid) {

		if (!world || !world->IsAlive(entity)) {
			return "Drop Hierarchy Entity";
		}
		if (const NameComponent* name = world->TryGetComponent<NameComponent>(entity)) {
			return std::format("{} ({})", name->name, ToString(uuid));
		}
		return std::format("Entity ({})", ToString(uuid));
	}

	void FillChannel(CurveChannel& channel, float value) {

		channel.defaultValue = value;
		channel.keys.clear();
		channel.AddKey(0.0f, value, CurveInterpolationMode::Linear);
	}

	void SetupTrackInitialValue(AnimationCurveTrack& track, const AnimationPropertyValue& value) {

		track.channels = MakeDefaultAnimationChannels(track.binding.valueType);

		// 追加直後は現在値を0秒キーとして置く。
		// duration終端キーは、最初の編集量を増やさないため今回は作らない。
		if (const float* valueFloat = std::get_if<float>(&value)) {
			FillChannel(track.channels[0], *valueFloat);
		} else if (const Vector2* valueVec2 = std::get_if<Vector2>(&value)) {
			FillChannel(track.channels[0], valueVec2->x);
			FillChannel(track.channels[1], valueVec2->y);
		} else if (const Vector3* valueVec3 = std::get_if<Vector3>(&value)) {
			FillChannel(track.channels[0], valueVec3->x);
			FillChannel(track.channels[1], valueVec3->y);
			FillChannel(track.channels[2], valueVec3->z);
		} else if (const Vector4* valueVec4 = std::get_if<Vector4>(&value)) {
			FillChannel(track.channels[0], valueVec4->x);
			FillChannel(track.channels[1], valueVec4->y);
			FillChannel(track.channels[2], valueVec4->z);
			FillChannel(track.channels[3], valueVec4->w);
		} else if (const Color3* valueColor3 = std::get_if<Color3>(&value)) {
			FillChannel(track.channels[0], valueColor3->r);
			FillChannel(track.channels[1], valueColor3->g);
			FillChannel(track.channels[2], valueColor3->b);
		} else if (const Color4* valueColor4 = std::get_if<Color4>(&value)) {
			FillChannel(track.channels[0], valueColor4->r);
			FillChannel(track.channels[1], valueColor4->g);
			FillChannel(track.channels[2], valueColor4->b);
			FillChannel(track.channels[3], valueColor4->a);
		} else if (const Quaternion* valueQuat = std::get_if<Quaternion>(&value)) {
			FillChannel(track.channels[0], valueQuat->x);
			FillChannel(track.channels[1], valueQuat->y);
			FillChannel(track.channels[2], valueQuat->z);
			FillChannel(track.channels[3], valueQuat->w);
		}
	}
}

Engine::AnimationClipTool::AnimationClipTool() {

	RegisterBuiltinAnimationProperties();
}

void AnimationClipTool::OpenEditorTool() {

	// ウィンドウ起動
	openWindow_ = true;
}

void AnimationClipTool::DrawEditorTool(const EditorToolContext& context) {

	if (!openWindow_) {
		EndPreviewAndRestore(context);
		return;
	}

	if (!ImGui::Begin("AnimationClip Clip", &openWindow_)) {
		ImGui::End();
		EndPreviewAndRestore(context);
		return;
	}

	//============================================================================
	//	AnimationClip編集UI
	//============================================================================

	DrawClipAssetUI(context);
	ImGui::Separator();

	DrawTargetEntityUI(context);
	ImGui::Separator();

	DrawPreviewControlUI(context);
	DrawPropertyListUI(context);
	DrawCurveEditorUI(context);

	// 再生中はTarget Entityへ直接適用する。確認はSceneView側の通常描画で行う。
	UpdatePreviewPlayback(context);

	ImGui::End();

	if (!openWindow_) {
		EndPreviewAndRestore(context);
	}
}

void AnimationClipTool::DrawClipAssetUI(const EditorToolContext& context) {

	AssetID before = clipAssetID_;
	const ValueEditResult result = MyGUI::AssetReferenceField("Set ClipData",
		clipAssetID_, context.toolContext.assetDatabase, { AssetType::AnimationClip });

	if (result.valueChanged || before != clipAssetID_) {

		// Clipを差し替える前に、前のClipで適用していたPreview値を必ず戻す。
		EndPreviewAndRestore(context);
		LoadClipFromSelectedAsset(context);
	}

	ImGui::SameLine();
	const bool canSave = clipAssetID_ && hasClip_;
	if (!canSave) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Save")) {
		SaveClipToSelectedAsset(context);
	}
	if (!canSave) {
		ImGui::EndDisabled();
	}

	if (hasClip_) {
		ImGui::Text("Clip: %s%s", clip_.name.c_str(), clipDirty_ ? " *" : "");
	} else {
		ImGui::TextDisabled("Clip is not set.");
	}

	if (!clipStatusText_.empty()) {
		ImGui::TextDisabled("%s", clipStatusText_.c_str());
	}
	if (!clipErrorText_.empty()) {
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
		ImGui::TextWrapped("%s", clipErrorText_.c_str());
		ImGui::PopStyleColor();
	}

	if (!hasClip_) {
		return;
	}

	// durationは0以下にすると評価やLoopの剰余で壊れるため、常に正値へ戻す。
	float duration = clip_.duration;
	if (ImGui::DragFloat("Duration", &duration, 0.01f, 0.01f, 10000.0f, "%.3f")) {
		clip_.duration = (std::max)(duration, 0.01f);
		previewTime_ = (std::clamp)(previewTime_, 0.0f, clip_.duration);
		curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, clip_.duration);
		clipDirty_ = true;
	}
	if (MyGUI::Checkbox("Loop", clip_.loop)) {
		clipDirty_ = true;
	}
}

void AnimationClipTool::DrawTargetEntityUI(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	const Entity targetEntity = GetTargetEntity(context);
	const std::string label = BuildTargetEntityLabel(world, targetEntity, targetEntityUUID_);

	ImGui::TextUnformatted("Set Target Entity");
	ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, ImGui::GetFrameHeight()));

	if (ImGui::BeginDragDropTarget()) {
		if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(IEditorPanel::kHierarchyDragDropPayloadType)) {
			if (payload->IsDelivery() && payload->DataSize == sizeof(UUID)) {

				const UUID droppedUUID = *static_cast<const UUID*>(payload->Data);
				const Entity droppedEntity = world ? world->FindByUUID(droppedUUID) : Entity::Null();
				if (world && world->IsAlive(droppedEntity)) {

					EndPreviewAndRestore(context);
					targetEntityUUID_ = droppedUUID;
					previewTime_ = hasClip_ ? (std::clamp)(previewTime_, 0.0f, clip_.duration) : 0.0f;
					curveState_.currentTime = previewTime_;
					// Targetをセットした直後から、Time 0.0fを含む現在時刻の値をSceneViewへ反映する。
					ApplyPreviewAtCurrentTime(context, true);
				}
			}
		}
		ImGui::EndDragDropTarget();
	}
}

void AnimationClipTool::DrawPreviewControlUI(const EditorToolContext& context) {

	if (!hasClip_) {
		return;
	}

	if (ImGui::Button(previewPlaying_ ? "Pause" : "Play")) {
		if (previewPlaying_) {
			previewPlaying_ = false;
		} else {
			if (clip_.duration <= previewTime_) {
				previewTime_ = 0.0f;
				curveState_.currentTime = previewTime_;
			}
			BeginPreview(context);
			previewPlaying_ = previewActive_;
			ApplyPreviewAtCurrentTime(context, true);
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Stop")) {
		EndPreviewAndRestore(context);
		previewTime_ = 0.0f;
		curveState_.currentTime = previewTime_;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(90.0f);
	ImGui::DragFloat("Speed", &previewSpeed_, 0.01f, 0.01f, 8.0f, "%.2f");

	float time = previewTime_;
	ImGui::SetNextItemWidth((std::max)(160.0f, ImGui::GetContentRegionAvail().x));
	if (ImGui::SliderFloat("Time", &time, 0.0f, clip_.duration, "%.3f")) {

		previewTime_ = (std::clamp)(time, 0.0f, clip_.duration);
		curveState_.currentTime = previewTime_;
		ApplyPreviewAtCurrentTime(context, true);
	}
	ImGui::TextDisabled("Time: %.3f / %.3f", previewTime_, clip_.duration);
}

void AnimationClipTool::DrawPropertyListUI(const EditorToolContext& context) {

	if (!hasClip_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity targetEntity = GetTargetEntity(context);

	ImGui::SeparatorText("Properties");
	if (!world || !world->IsAlive(targetEntity)) {
		ImGui::BeginDisabled();
	}
	if (ImGui::Button("Add Property")) {
		ImGui::OpenPopup("AnimationClipAddProperty");
	}
	if (!world || !world->IsAlive(targetEntity)) {
		ImGui::EndDisabled();
	}

	if (ImGui::BeginPopup("AnimationClipAddProperty")) {
		if (!world || !world->IsAlive(targetEntity)) {
			ImGui::TextDisabled("Target Entity is not set.");
		} else {
			std::unordered_map<std::string, std::vector<const AnimationPropertyDescriptor*>> groups{};
			for (const AnimationPropertyDescriptor* desc :
				AnimationPropertyRegistry::GetInstance().GetPropertiesForEntity(*world, targetEntity)) {
				groups[desc->componentName].emplace_back(desc);
			}

			for (auto& [componentName, properties] : groups) {
				if (!ImGui::BeginMenu(componentName.c_str())) {
					continue;
				}
				for (const AnimationPropertyDescriptor* desc : properties) {

					const bool exists = std::any_of(clip_.curveTracks.begin(), clip_.curveTracks.end(),
						[desc](const AnimationCurveTrack& track) {
							return track.binding.componentName == desc->componentName &&
								track.binding.propertyPath == desc->propertyPath;
						});
					if (exists) {
						ImGui::BeginDisabled();
					}
					if (ImGui::MenuItem(desc->displayName.c_str())) {
						AddPropertyTrack(*desc, *world, targetEntity);
						ApplyPreviewAtCurrentTime(context, true);
					}
					if (exists) {
						ImGui::EndDisabled();
					}
				}
				ImGui::EndMenu();
			}
		}
		ImGui::EndPopup();
	}

	if (clip_.curveTracks.empty()) {
		ImGui::TextDisabled("No property tracks.");
		return;
	}

	NormalizeSelectedTrackIndex();

	for (size_t i = 0; i < clip_.curveTracks.size();) {

		AnimationCurveTrack& track = clip_.curveTracks[i];
		const AnimationPropertyDescriptor* desc = AnimationPropertyRegistry::GetInstance().Find(
			track.binding.componentName, track.binding.propertyPath);
		const bool missing = !world || !world->IsAlive(targetEntity) ||
			!desc || !desc->hasComponent || !desc->hasComponent(*world, targetEntity);

		ImGui::PushID(static_cast<int>(i));
		const bool selected = selectedTrackIndex_ == static_cast<int>(i);

		if (missing) {
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
		}
		const std::string label = BuildTrackLabel(track);
		const float rowWidth = (std::max)(120.0f, ImGui::GetContentRegionAvail().x - 250.0f);
		if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_None, ImVec2(rowWidth, 0.0f))) {
			selectedTrackIndex_ = static_cast<int>(i);
			curveState_.ClearSelection();
			curveState_.frameSelectionRequest = true;
		}
		if (missing) {
			ImGui::PopStyleColor();
			if (ImGui::BeginItemTooltip()) {
				ImGui::TextUnformatted("Target Entity does not have this property.");
				ImGui::EndTooltip();
			}
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		int applyModeIndex = static_cast<int>(track.applyMode);
		if (track.binding.valueType == AnimationValueType::Quaternion) {
			ImGui::BeginDisabled();
			applyModeIndex = 0;
			ImGui::Combo("Apply", &applyModeIndex, kApplyModeNames.data(), static_cast<int>(kApplyModeNames.size()));
			ImGui::EndDisabled();
			track.applyMode = AnimationApplyMode::Override;
		} else if (ImGui::Combo("Apply", &applyModeIndex, kApplyModeNames.data(), static_cast<int>(kApplyModeNames.size()))) {
			track.applyMode = static_cast<AnimationApplyMode>(applyModeIndex);
			clipDirty_ = true;
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("Remove")) {
			EndPreviewAndRestore(context);
			clip_.curveTracks.erase(clip_.curveTracks.begin() + i);
			curveState_.ClearSelection();
			if (selectedTrackIndex_ == static_cast<int>(i)) {
				selectedTrackIndex_ = clip_.curveTracks.empty() ? -1 :
					(std::min)(static_cast<int>(i), static_cast<int>(clip_.curveTracks.size()) - 1);
			} else if (static_cast<int>(i) < selectedTrackIndex_) {
				--selectedTrackIndex_;
			}
			clipDirty_ = true;
			ImGui::PopID();
			continue;
		}
		ImGui::PopID();
		++i;
	}
}

void AnimationClipTool::DrawCurveEditorUI(const EditorToolContext& context) {

	if (!hasClip_) {
		return;
	}

	std::vector<CurveChannelRef> channelRefs{};
	NormalizeSelectedTrackIndex();

	if (selectedTrackIndex_ < 0) {
		ImGui::SeparatorText("Curve Editor");
		ImGui::TextDisabled("Select property track.");
		return;
	}

	AnimationCurveTrack& track = clip_.curveTracks[static_cast<size_t>(selectedTrackIndex_)];
	{
		for (CurveChannel& channel : track.channels) {
			CurveChannelRef ref{};
			ref.channel = &channel;
			ref.displayName = BuildTrackLabel(track) + "." + channel.name;
			channelRefs.emplace_back(std::move(ref));
		}
	}

	ImGui::SeparatorText("Curve Editor");
	if (channelRefs.empty()) {
		ImGui::TextDisabled("Visible curve track is empty.");
		return;
	}

	curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, clip_.duration);
	curveState_.currentTime = previewTime_;

	const float previousTime = curveState_.currentTime;
	const CurveEditResult result = MyGUI::CurveEditor("AnimationClipCurveEditor",
		channelRefs, curveState_, CurveEditSetting{ .size = ImVec2(0.0f, 360.0f), .autoFit = false });

	if (result.valueChanged || result.editFinished) {
		clipDirty_ = true;
	}
	if (previousTime != curveState_.currentTime || result.valueChanged) {
		previewTime_ = (std::clamp)(curveState_.currentTime, 0.0f, clip_.duration);
		curveState_.currentTime = previewTime_;
		ApplyPreviewAtCurrentTime(context, true);
	}
}

void AnimationClipTool::LoadClipFromSelectedAsset(const EditorToolContext& context) {

	clipErrorText_.clear();
	clipStatusText_.clear();
	hasClip_ = false;
	clipDirty_ = false;
	loadedClipAssetID_ = {};

	if (!clipAssetID_) {
		clip_ = AnimationClipAsset{};
		return;
	}

	const AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database) {
		clipErrorText_ = "AssetDatabase is not available.";
		return;
	}

	const std::filesystem::path path = database->ResolveFullPath(clipAssetID_);
	if (path.empty()) {
		clipErrorText_ = "AnimationClip asset path was not found.";
		return;
	}

	AnimationClipAsset loaded{};
	if (!LoadAnimationClipAsset(path, loaded)) {
		clipErrorText_ = "Failed to load AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipTool failed to load clip: {}", path.string());
		return;
	}

	clip_ = std::move(loaded);
	clip_.guid = clipAssetID_;
	if (clip_.name.empty()) {
		clip_.name = path.stem().string();
	}
	if (clip_.duration <= 0.0f) {
		clip_.duration = 1.0f;
	}
	for (AnimationCurveTrack& track : clip_.curveTracks) {
		NormalizeAnimationTrackChannels(track);
	}

	hasClip_ = true;
	loadedClipAssetID_ = clipAssetID_;
	selectedTrackIndex_ = clip_.curveTracks.empty() ? -1 : 0;
	previewTime_ = (std::clamp)(previewTime_, 0.0f, clip_.duration);
	curveState_.currentTime = previewTime_;
	curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, clip_.duration);
	curveState_.ClearSelection();
	clipStatusText_ = "Clip loaded.";
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipTool::SaveClipToSelectedAsset(const EditorToolContext& context) {

	clipErrorText_.clear();
	clipStatusText_.clear();

	if (!clipAssetID_) {
		clipErrorText_ = "ClipData is not set.";
		return;
	}

	const AssetDatabase* database = context.toolContext.assetDatabase;
	if (!database) {
		clipErrorText_ = "AssetDatabase is not available.";
		return;
	}

	const std::filesystem::path path = database->ResolveFullPath(clipAssetID_);
	if (path.empty()) {
		clipErrorText_ = "AnimationClip asset path was not found.";
		return;
	}

	clip_.guid = clipAssetID_;
	if (clip_.name.empty()) {
		clip_.name = path.stem().string();
	}
	clip_.duration = (std::max)(clip_.duration, 0.01f);
	for (AnimationCurveTrack& track : clip_.curveTracks) {
		NormalizeAnimationTrackChannels(track);
	}

	if (!SaveAnimationClipAsset(path, clip_)) {
		clipErrorText_ = "Failed to save AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipTool failed to save clip: {}", path.string());
		return;
	}

	clipDirty_ = false;
	clipStatusText_ = "Clip saved.";
}

void AnimationClipTool::AddPropertyTrack(const AnimationPropertyDescriptor& desc,
	ECSWorld& world, const Entity& entity) {

	if (!hasClip_ || !world.IsAlive(entity) || !desc.getValue) {
		return;
	}

	AnimationPropertyValue currentValue{};
	if (!desc.getValue(world, entity, currentValue)) {
		return;
	}

	AnimationCurveTrack track{};
	track.binding.componentName = desc.componentName;
	track.binding.propertyPath = desc.propertyPath;
	track.binding.valueType = desc.valueType;
	track.applyMode = AnimationApplyMode::Override;
	track.visible = true;

	SetupTrackInitialValue(track, currentValue);
	NormalizeAnimationTrackChannels(track);
	selectedTrackIndex_ = static_cast<int>(clip_.curveTracks.size());
	clip_.curveTracks.emplace_back(std::move(track));
	clipDirty_ = true;
	curveState_.ClearSelection();
	curveState_.frameSelectionRequest = true;
}

Entity AnimationClipTool::GetTargetEntity(const EditorToolContext& context) const {

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return Entity::Null();
	}

	const Entity entity = world->FindByUUID(targetEntityUUID_);
	return world->IsAlive(entity) ? entity : Entity::Null();
}

void AnimationClipTool::NormalizeSelectedTrackIndex() {

	if (clip_.curveTracks.empty()) {
		selectedTrackIndex_ = -1;
		return;
	}
	if (selectedTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= selectedTrackIndex_) {
		selectedTrackIndex_ = 0;
	}
}

void AnimationClipTool::SyncCurveStateTime() {

	if (!hasClip_) {
		previewTime_ = 0.0f;
		curveState_.currentTime = 0.0f;
		return;
	}

	previewTime_ = (std::clamp)(previewTime_, 0.0f, clip_.duration);
	curveState_.currentTime = previewTime_;
}

void AnimationClipTool::UpdatePreviewPlayback(const EditorToolContext& context) {

	if (!previewPlaying_ || !hasClip_) {
		return;
	}

	float deltaTime = ImGui::GetIO().DeltaTime;
	if (deltaTime <= 0.0f) {
		deltaTime = context.toolContext.deltaTime;
	}
	previewTime_ += deltaTime * previewSpeed_;
	if (clip_.duration <= previewTime_) {
		if (clip_.loop && 0.0f < clip_.duration) {
			previewTime_ = std::fmod(previewTime_, clip_.duration);
		} else {
			previewTime_ = clip_.duration;
			previewPlaying_ = false;
		}
	}

	SyncCurveStateTime();
	ApplyPreviewAtCurrentTime(context, false);
}

void AnimationClipTool::ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive) {

	if (!hasClip_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return;
	}

	// ScrubだけでもPreview扱いにして、Stop/Closeで元の値に戻せるようにする。
	if (!previewActive_) {
		BeginPreview(context);
	}
	if (!previewActive_) {
		return;
	}

	AnimationClipEvaluator::ApplyClip(*world, entity, clip_, previewTime_, previewBaseValues_);
	if (!keepActive && !previewPlaying_) {
		EndPreviewAndRestore(context);
	}
}

void AnimationClipTool::BeginPreview(const EditorToolContext& context) {

	if (previewActive_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity) || !hasClip_) {
		return;
	}

	CachePreviewBaseValues(*world, entity);
	previewActive_ = true;
}

void AnimationClipTool::EndPreviewAndRestore(const EditorToolContext& context) {

	if (!previewActive_) {
		previewPlaying_ = false;
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (world && world->IsAlive(entity)) {
		RestorePreviewBaseValues(*world, entity);
	}

	previewBaseValues_.clear();
	previewActive_ = false;
	previewPlaying_ = false;
}

void AnimationClipTool::CachePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	previewBaseValues_.clear();
	for (const AnimationCurveTrack& track : clip_.curveTracks) {

		const AnimationPropertyDescriptor* desc = AnimationPropertyRegistry::GetInstance().Find(
			track.binding.componentName, track.binding.propertyPath);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}

		AnimationPreviewBaseValue baseValue{};
		baseValue.binding = track.binding;
		if (desc->getValue(world, entity, baseValue.value)) {
			previewBaseValues_.emplace_back(std::move(baseValue));
		}
	}
}

void AnimationClipTool::RestorePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	for (const AnimationPreviewBaseValue& baseValue : previewBaseValues_) {

		const AnimationPropertyDescriptor* desc = AnimationPropertyRegistry::GetInstance().Find(
			baseValue.binding.componentName, baseValue.binding.propertyPath);
		if (!desc || !desc->setValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}

		// Tool Previewで触った値だけを元に戻す。本番適用はController側に任せる。
		desc->setValue(world, entity, baseValue.value);
	}
}
