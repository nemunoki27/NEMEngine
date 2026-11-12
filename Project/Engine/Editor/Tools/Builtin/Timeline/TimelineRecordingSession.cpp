#include "TimelineRecordingSession.h"
#include "TimelineEditorFields.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/ECS/World/ECSWorldLifetime.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditorUtility.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditorUI.h>
#include <Engine/Editor/UI/ImGui/ImGuiHelpers.h>

// c++
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace Engine;

namespace Engine {

	struct TimelineRecordingState final : IAnimationPropertyRecorder {

		std::shared_ptr<AnimationClipDocument> document;
		ECSWorld* world = nullptr;
		std::weak_ptr<const ECSWorldLifetime> lifetime;
		UUID assetTrack{}, assetClip{}, target{};
		double sourceTime = 0.0, frameRate = 60.0;
		std::string error;

		bool Record(std::span<const RecordedAnimationProperty> changes) override {

			if (!RecordAnimationProperties(document->GetClip(), changes, sourceTime, frameRate)) {
				error = "現在の適用設定ではこの値を記録できません";
				return false;
			}
			error.clear();
			document->MarkDirty();
			document->CommitHistory();
			return true;
		}

		bool RestoreHistory(bool redo) override {

			return document->RestoreHistory(redo);
		}
	};
}

namespace {

	// 選択中のクリップか再生位置のクリップを永続IDから解決する
	std::pair<TimelineTrack*, TimelineClip*> FindSource(TimelineAsset& asset, Engine::UUID trackID, Engine::UUID clipID, double time) {

		const auto track = std::ranges::find_if(asset.tracks, [&](const auto& item) { return item.id == trackID; });
		if (track == asset.tracks.end() || track->kind != TimelineTrackKind::Property || track->locked) return {};
		for (auto& clip : track->clips) if (clip.id == clipID) return {&*track, &clip};
		for (auto& clip : track->clips) if (time >= clip.start && time <= clip.start + clip.duration) return {&*track, &clip};
		return {&*track, track->clips.empty() ? nullptr : &track->clips.front()};
	}

	// 配置時刻を素材の時刻へ変換する
	double SourceTime(const TimelineClip& clip, double time) {

		return clip.clipIn + std::clamp(time - clip.start, 0.0, clip.duration) * clip.speed;
	}

	// インライン補間の値を既存の型へ変換する
	AnimationPropertyValue ReadValue(AnimationValueType type, const nlohmann::json& value) {

		switch (type) {
		case AnimationValueType::Float: return value.get<float>();
		case AnimationValueType::Int: return value.get<int32_t>();
		case AnimationValueType::Vector2: return Vector2::FromJson(value);
		case AnimationValueType::Vector3: return Vector3::FromJson(value);
		case AnimationValueType::Vector4: return Vector4::FromJson(value);
		case AnimationValueType::Color3: return Color3::FromJson(value);
		case AnimationValueType::Color4: return Color4::FromJson(value);
		case AnimationValueType::Quaternion: return Quaternion::FromJson(value);
		}
		throw std::invalid_argument("対応していない記録型です");
	}

	// 記録へ切り替える前の補間を素材キーへ保持する
	void PreserveInlineSource(const TimelineClip& source, AnimationClipAsset& clip, double frameRate) {

		if (!source.data.contains("binding") || !source.data.contains("from") || !source.data.contains("to")) return;
		AnimationCurveTrack track;
		track.binding = source.data.at("binding").get<AnimationPropertyBinding>();
		const auto from = ReadValue(track.binding.valueType, source.data.at("from"));
		const auto to = ReadValue(track.binding.valueType, source.data.at("to"));
		AnimationClipEditorUtility::SetupTrackInitialValue(track, from);
		const double duration = source.sourceDuration > 0.0 ? source.sourceDuration : source.duration;
		const int count = std::clamp(static_cast<int>(std::ceil((std::min)(duration * frameRate, 600.0))), 1, 600);
		for (int i = 0; i <= count; ++i) {
			const float phase = static_cast<float>(i) / static_cast<float>(count);
			AnimationPropertyValue value;
			if (AnimationValueOperations::LerpValue(from, to, EasedValue(source.easing, phase), value))
				SetAnimationPropertyKey(track, static_cast<float>(duration * phase), value);
		}
		clip.curveTracks.push_back(std::move(track));
		clip.duration = static_cast<float>(duration);
	}
}

TimelineRecordingSession::~TimelineRecordingSession() {

	EndRecording();
}

std::shared_ptr<AnimationClipDocument> TimelineRecordingSession::OpenDocument(
	const EditorToolContext& context, const TimelineClip& clip) {

	const AssetID source = ParseAssetID(clip.data, "asset");
	const auto store = GetAnimationClipDocumentStore(context);
	if (!source || !store || !context.toolContext.assetDatabase) return {};
	const auto document = store->Acquire(*context.toolContext.assetDatabase, source, error_);
	if (document && std::ranges::find(documents_, document) == documents_.end()) documents_.push_back(document);
	return document;
}

bool TimelineRecordingSession::BeginRecording(const EditorToolContext& context, const TimelineTrack& track,
	const TimelineClip& clip, Entity target, double time, double frameRate) {

	auto document = OpenDocument(context, clip);
	if (!document || !context.GetWorld() || !context.GetWorld()->IsAlive(target) || clip.speed <= 0.0) return false;
	EndRecording();
	recording_ = std::make_shared<TimelineRecordingState>();
	recording_->document = std::move(document);
	recording_->world = context.GetWorld();
	recording_->lifetime = context.GetWorld()->GetLifetime();
	recording_->assetTrack = track.id;
	recording_->assetClip = clip.id;
	recording_->target = context.GetWorld()->GetUUID(target);
	recording_->sourceTime = SourceTime(clip, time);
	recording_->frameRate = frameRate;
	AnimationPropertyQueryContext query;
	query.assetDatabase = context.toolContext.assetDatabase;
	query.renderPipeline = context.panelContext ? context.panelContext->renderPipeline : nullptr;
	AnimationPropertyEditCapture::SetRecorder(*context.GetWorld(), target, recording_, query);
	error_.clear();
	return true;
}

bool TimelineRecordingSession::ToggleRecording(const EditorToolContext& context, TimelineAsset& asset,
	UUID trackID, UUID clipID, Entity target, double time) {

	if (IsRecording(trackID)) { EndRecording(); return false; }
	if (context.IsPlaying() || !context.CanEditScene() || !context.GetWorld() || !context.GetWorld()->IsAlive(target)) return false;
	const auto [track, clip] = FindSource(asset, trackID, clipID, time);
	if (!track) return false;
	if (clip && ParseAssetID(clip->data, "asset")) return BeginRecording(context, *track, *clip, target, time, asset.frameRate);
	pendingTrack_ = trackID;
	pendingClip_ = clip ? clip->id : UUID{};
	pendingTarget_ = context.GetWorld()->GetUUID(target);
	pendingTime_ = time;
	creationDialog_.Open();
	return false;
}

void TimelineRecordingSession::Update(const EditorToolContext& context, TimelineAsset& asset, double time, bool playing) {

	if (recording_) {
		const auto lifetime = recording_->lifetime.lock();
		const auto [track, clip] = FindSource(asset, recording_->assetTrack, recording_->assetClip, time);
		if (playing || context.IsPlaying() || !lifetime || !lifetime->IsAlive() || recording_->world != context.GetWorld() ||
			!track || !clip || !context.GetWorld()->IsAlive(context.GetWorld()->FindByUUID(recording_->target)) ||
			ParseAssetID(clip->data, "asset") != recording_->document->GetClip().guid) EndRecording();
		else { recording_->sourceTime = SourceTime(*clip, time); recording_->frameRate = asset.frameRate; }
	}
	if (const auto store = GetAnimationClipDocumentStore(context); store && context.GetWorld()) store->Publish(*context.GetWorld());
}

void TimelineRecordingSession::EndRecording() {

	if (!recording_) return;
	const auto lifetime = recording_->lifetime.lock();
	if (lifetime && lifetime->IsAlive()) AnimationPropertyEditCapture::ClearRecorder(*recording_->world, recording_.get());
	recording_.reset();
}

void TimelineRecordingSession::DrawCreateDialog(const EditorToolContext& context, TimelineAsset& asset) {

	creationDialog_.Draw("記録用アニメーションクリップを作成", ProjectAssetFileKind::AnimationClip,
		[&](const std::string& path, std::string& diagnostic) {
			const auto [track, selected] = FindSource(asset, pendingTrack_, pendingClip_, pendingTime_);
			ECSWorld* world = context.GetWorld();
			const Entity target = world ? world->FindByUUID(pendingTarget_) : Entity::Null();
			if (!track || !world || !world->IsAlive(target) || context.IsPlaying()) {
				diagnostic = "記録対象が変更されています"; return false;
			}
			AnimationClipAsset preserved;
			bool restore = true;
			try {
				if (selected) {
					PreserveInlineSource(*selected, preserved, asset.frameRate);
					restore = selected->data.value("restoreOnEnd", true);
				}
			} catch (const std::exception& error) { diagnostic = error.what(); return false; }
			const AssetID source = ProjectAssetFileUtility::CreateRegisteredAsset(*context.toolContext.assetDatabase,
				"GameAssets", ProjectAssetFileKind::AnimationClip, path, diagnostic);
			if (!source) return false;
			TimelineClip* clip = selected;
			if (!clip) {
				TimelineClip added;
				added.id = UUID::New(); added.name = "記録"; added.duration = asset.duration;
				track->clips.push_back(std::move(added));
				clip = &track->clips.back();
			}
			clip->data = {{"asset", ToAssetReferenceJson(source)}, {"restoreOnEnd", restore}};
			const auto document = OpenDocument(context, *clip);
			if (!document) { diagnostic = error_; return false; }
			document->GetClip().curveTracks = std::move(preserved.curveTracks);
			document->GetClip().duration = (std::max)(preserved.duration, static_cast<float>((std::max)(clip->duration, 0.01)));
			document->MarkDirty();
			document->CommitHistory();
			assetChanged_ = true;
			return BeginRecording(context, *track, *clip, target, pendingTime_, asset.frameRate);
		});
}

bool TimelineRecordingSession::DrawCurves(const EditorToolContext& context, TimelineAsset& asset, UUID trackID,
	UUID clipID, Entity target, double& time, ImVec2 size) {

	const auto [track, clip] = FindSource(asset, trackID, clipID, time);
	if (!track || !clip) return false;
	const auto document = OpenDocument(context, *clip);
	if (!document) return false;
	auto& view = curveSessions_[clip->id];
	if (!view) view = std::make_unique<AnimationClipEditSession>();
	auto& curveSession = *view;
	const bool sourceChanged = curveSession.GetDocument() != document;
	const auto top = ImGui::GetCursorScreenPos();
	const float width = size.x > 0.0f ? size.x : ImGui::GetContentRegionAvail().x;
	ImGui::PushID(ToString(clip->id).c_str());
	const bool visible = ImGui::BeginChild("TimelineTrackCurves", ImVec2(width, size.y), ImGuiChildFlags_None,
		ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	if (!visible) { ImGui::EndChild(); ImGui::PopID(); return true; }
	curveSession.SetExternalEvaluation(true);
	curveSession.AttachDocument(document);
	if (context.GetWorld() && context.GetWorld()->IsAlive(target))
		curveSession.SetPreviewTarget(context, context.GetWorld()->GetUUID(target));
	curveSession.NormalizeSelectedTrackIndex();
	if (sourceChanged) curveSession.GetCurveState().frameSelectionRequest = true;
	std::vector<std::string> labels;
	std::vector<const char*> names;
	for (const auto& property : document->GetClip().curveTracks)
		labels.push_back(property.binding.componentName + "." + property.binding.propertyPath);
	for (const auto& label : labels) names.push_back(label.c_str());
	if (!names.empty()) TimelineEditorFields::Select("プロパティ", curveSession.GetSelectedTrackIndex(),
		names.data(), static_cast<int>(names.size()));
	if (curveSession.GetEditorViewTrackIndex() != curveSession.GetSelectedTrackIndex()) {
		curveSession.LoadSelectedTrackEditorView();
		curveSession.GetCurveState().frameSelectionRequest = true;
	}
	auto& fitRevision = curveFitRevisions_[clip->id];
	if (IsRecording(trackID) && fitRevision != document->GetRevision() && !ImGui::IsAnyItemActive())
		curveSession.GetCurveState().frameSelectionRequest = true;
	fitRevision = document->GetRevision();
	curveSession.GetPreviewTime() = static_cast<float>(SourceTime(*clip, time));
	const double previous = curveSession.GetPreviewTime();
	CurveEditSetting setting;
	setting.size = ImVec2(0.0f, (std::max)(1.0f, ImGui::GetContentRegionAvail().y));
	setting.showSidePanels = false;
	setting.showToolbar = false;
	setting.snapInterval = static_cast<float>(1.0 / asset.frameRate);
	AnimationClipEditorUI::DrawCurveEditorUI(curveSession, context, &setting);
	if (!ImGui::IsAnyItemActive()) document->CommitHistory();
	if (previous != curveSession.GetPreviewTime() && clip->speed > 0.0)
		time = std::clamp(clip->start + (curveSession.GetPreviewTime() - clip->clipIn) / clip->speed, 0.0, asset.duration);
	const auto& mouse = ImGui::GetIO().MousePos;
	if (mouse.x >= top.x && mouse.x <= top.x + width && mouse.y >= top.y && mouse.y <= top.y + size.y &&
		ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
		(ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
		curveTrackID_ = trackID;
		curveClipID_ = clip->id;
	}
	ImGui::EndChild();
	ImGui::PopID();
	return true;
}

bool TimelineRecordingSession::Save(const EditorToolContext& context) {

	auto* manager = context.toolContext.systemContext ? context.toolContext.systemContext->animationClipManager : nullptr;
	for (const auto& document : documents_) if (document->IsDirty() &&
		!document->Save(*context.toolContext.assetDatabase, manager)) { error_ = document->GetError(); return false; }
	error_.clear();
	return true;
}

bool TimelineRecordingSession::HasSelectedKeys() const {

	const auto found = curveSessions_.find(curveClipID_);
	return found != curveSessions_.end() && !found->second->GetCurveState().selectedKeys.empty();
}

bool TimelineRecordingSession::DrawKeyInspector(const EditorToolContext& context) {

	const auto found = curveSessions_.find(curveClipID_);
	if (found == curveSessions_.end() || !HasSelectedKeys()) return false;
	AnimationClipEditorUI::DrawKeyInspectorUI(*found->second, context);
	if (!ImGui::IsAnyItemActive()) found->second->CommitHistory();
	return true;
}

void TimelineRecordingSession::Discard() {

	EndRecording();
	for (const auto& document : documents_) if (document->IsDirty()) document->Discard();
}

bool TimelineRecordingSession::RestoreHistory(bool redo) {

	const auto found = curveSessions_.find(curveClipID_);
	const auto document = recording_ ? recording_->document :
		found != curveSessions_.end() ? found->second->GetDocument() : nullptr;
	return document && document->RestoreHistory(redo);
}

bool TimelineRecordingSession::IsRecording(UUID trackID) const {

	return recording_ && (!trackID || recording_->assetTrack == trackID);
}

bool TimelineRecordingSession::HasPendingEdits() const {

	return std::ranges::any_of(documents_, [](const auto& document) { return document->IsDirty(); });
}

const std::string& TimelineRecordingSession::GetError() const {

	return recording_ && !recording_->error.empty() ? recording_->error : error_;
}
