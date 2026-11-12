#include "AnimationClipEditSession.h"
#include "AnimationClipEditorUtility.h"
#include <Engine/Core/Animation/Evaluation/AnimationValueOperations.h>

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>

// c++
#include <algorithm>
#include <utility>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

//============================================================================
//	AnimationClipEditSession
//============================================================================

AnimationClipEditSession::~AnimationClipEditSession() {

	EndPreviewAndRestore();
}

void AnimationClipEditSession::AddPropertyTrack(
	const AnimationPropertyDescriptor& desc, ECSWorld& world, const Entity& entity) {

	if (!hasClip_ || !world.IsAlive(entity) || !desc.getValue) {
		return;
	}

	const AnimationPropertyBinding binding{desc.componentName, desc.propertyPath, desc.valueType};
	if (std::any_of(document_->GetClip().curveTracks.begin(), document_->GetClip().curveTracks.end(), [&](const auto& track) {
		return AnimationValueOperations::SameBinding(track.binding, binding);
	})) return;

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

	// 現在値を初期値へ写し、任意軸の回転は軸キーも保持する
	SetupTrackInitialValue(track, currentValue);
	NormalizeAnimationTrackChannels(track);
	selectedTrackIndex_ = static_cast<int>(document_->GetClip().curveTracks.size());
	document_->GetClip().curveTracks.emplace_back(std::move(track));
	// 追加したPropertyの開始値を保持する
	CachePreviewBaseValues(world, entity);
	LoadSelectedTrackEditorView();
	MarkClipDirty();
	curveState_.ClearSelection();
	curveState_.frameSelectionRequest = true;
}

Entity AnimationClipEditSession::GetTargetEntity(const EditorToolContext& context) const {

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return Entity::Null();
	}

	const Entity entity = world->FindByUUID(targetEntityUUID_);
	return world->IsAlive(entity) ? entity : Entity::Null();
}

AnimationClipDetectedDimension AnimationClipEditSession::DetectTargetDimension(const EditorToolContext& context) const {

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return AnimationClipDetectedDimension::Unknown;
	}

	const bool has2D = world->HasComponent<SpriteRendererComponent>(entity) ||
					   world->HasComponent<TextRendererComponent>(entity) ||
					   world->HasComponent<OrthographicCameraComponent>(entity);
	const bool has3D =
		world->HasComponent<MeshRendererComponent>(entity) || world->HasComponent<PerspectiveCameraComponent>(entity);
	// 描画Componentから用途を推定し、Add Propertyの候補を2D/3Dに寄せる
	if (has2D && has3D) {
		return AnimationClipDetectedDimension::Mixed;
	}
	if (has2D) {
		return AnimationClipDetectedDimension::Mode2D;
	}
	return AnimationClipDetectedDimension::Mode3D;
}

AnimationClipEditDimension AnimationClipEditSession::GetEffectiveEditDimension(const EditorToolContext& context) const {

	if (editDimension_ == AnimationClipEditDimension::Mode2D || editDimension_ == AnimationClipEditDimension::Mode3D) {
		return editDimension_;
	}

	const AnimationClipDetectedDimension detected = DetectTargetDimension(context);
	return detected == AnimationClipDetectedDimension::Mode2D ? AnimationClipEditDimension::Mode2D
															  : AnimationClipEditDimension::Mode3D;
}

void AnimationClipEditSession::NormalizeSelectedTrackIndex() {

	if (document_->GetClip().curveTracks.empty()) {
		selectedTrackIndex_ = -1;
		return;
	}
	if (selectedTrackIndex_ < 0 || static_cast<int>(document_->GetClip().curveTracks.size()) <= selectedTrackIndex_) {
		selectedTrackIndex_ = 0;
	}
}

void AnimationClipEditSession::StoreSelectedTrackEditorView() {

	if (editorViewTrackIndex_ < 0 || static_cast<int>(document_->GetClip().curveTracks.size()) <= editorViewTrackIndex_) {
		return;
	}

	// Trackを切り替えても、各Propertyごとの表示範囲を保持する
	AnimationTrackEditorView& view = document_->GetClip().curveTracks[static_cast<size_t>(editorViewTrackIndex_)].editorView;
	view.timeMin = curveState_.visibleTimeMin;
	view.timeMax = curveState_.visibleTimeMax;
	view.valueMin = curveState_.visibleValueMin;
	view.valueMax = curveState_.visibleValueMax;
}

void AnimationClipEditSession::LoadSelectedTrackEditorView() {

	if (selectedTrackIndex_ < 0 || static_cast<int>(document_->GetClip().curveTracks.size()) <= selectedTrackIndex_) {
		editorViewTrackIndex_ = -1;
		return;
	}

	const AnimationTrackEditorView& view = document_->GetClip().curveTracks[static_cast<size_t>(selectedTrackIndex_)].editorView;
	curveState_.visibleTimeMin = view.timeMin;
	curveState_.visibleTimeMax = (std::max)(view.timeMax, view.timeMin + 0.001f);
	curveState_.visibleValueMin = view.valueMin;
	curveState_.visibleValueMax = (std::max)(view.valueMax, view.valueMin + 0.001f);
	// 1ms単位で時刻を合わせる
	curveState_.snapEnabled = true;
	curveState_.snapInterval = 0.001f;
	editorViewTrackIndex_ = selectedTrackIndex_;
}

void AnimationClipEditSession::SyncCurveStateTime() {

	if (!hasClip_) {
		previewTime_ = 0.0f;
		curveState_.currentTime = 0.0f;
		return;
	}

	previewTime_ = (std::clamp)(previewTime_, 0.0f, AnimationClipEvaluator::GetPlaybackDuration(document_->GetClip()));
	curveState_.currentTime = previewTime_;
}

void AnimationClipEditSession::UpdateAutoDurationAndPreview(const EditorToolContext& context) {

	// 再生時間とプレビューを編集結果に合わせる
	UpdateAnimationClipAutoDuration(document_->GetClip());
	previewTime_ = (std::clamp)(previewTime_, 0.0f, AnimationClipEvaluator::GetPlaybackDuration(document_->GetClip()));
	curveState_.currentTime = previewTime_;
	MarkClipDirty();
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipEditSession::CommitHistory() {

	if (hasClip_) document_->CommitHistory();
}

bool AnimationClipEditSession::RestoreHistory(bool redo) {

	EndPreviewAndRestore();
	if (!document_->RestoreHistory(redo)) return false;
	NormalizeSelectedTrackIndex();
	curveState_.ClearSelection();
	editorViewTrackIndex_ = -1;
	return true;
}

void AnimationClipEditSession::AttachDocument(std::shared_ptr<AnimationClipDocument> document) {

	if (!document || document_ == document) return;
	EndPreviewAndRestore();
	previewBaseValues_.clear();
	document_ = std::move(document);
	clipAssetID_ = document_->GetClip().guid;
	loadedClipAssetID_ = clipAssetID_;
	hasClip_ = true;
	editorViewTrackIndex_ = -1;
	selectedTrackIndex_ = document_->GetClip().curveTracks.empty() ? -1 : 0;
	previewTime_ = (std::clamp)(previewTime_, 0.0f, document_->GetClip().duration);
	curveState_.currentTime = previewTime_;
	curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, document_->GetClip().duration);
	curveState_.ClearSelection();
	LoadSelectedTrackEditorView();
}
