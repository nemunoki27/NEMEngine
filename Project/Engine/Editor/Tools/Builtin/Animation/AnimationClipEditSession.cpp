#include "AnimationClipEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Tools/ImGui/ImGuiEnum.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Tools/ImGui/ImGuiHelpers.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

#include <Engine/Core/Animation/Curves/QuaternionAxisKeyUtility.h>

// imgui
// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <string_view>
#include <unordered_map>

#include <imgui.h>

#include "AnimationClipEditorUtility.h"

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

void AnimationClipEditSession::LoadClipFromSelectedAsset(const EditorToolContext& context) {

	// 読み込み失敗時に前回のClip状態が残らないよう、先にUI状態を初期化する
	clipErrorText_.clear();
	hasClip_ = false;
	clipDirty_ = false;
	loadedClipAssetID_ = {};
	// Clipが変わるとtrack構成も変わるため、旧Clipの基準値は破棄して次のPreviewで捕捉し直す
	previewBaseValues_.clear();

	if (!clipAssetID_) {
		clip_ = AnimationClipAsset{};
		return;
	}

	const AssetDatabase* database = context.toolContext.assetDatabase;
	const std::filesystem::path path = database->ResolveFullPath(clipAssetID_);
	if (path.empty()) {
		clipErrorText_ = "AnimationClip asset path was not found.";
		return;
	}

	AnimationClipAsset loaded{};
	if (!LoadAnimationClipAsset(path, loaded)) {
		clipErrorText_ = "Failed to load AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipToolでClipを読み込めません: {}", path.string());
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
		// Runtime評価前にChannel数を現在仕様へ揃える
		NormalizeAnimationTrackChannels(track);
	}

	hasClip_ = true;
	loadedClipAssetID_ = clipAssetID_;
	selectedTrackIndex_ = clip_.curveTracks.empty() ? -1 : 0;
	previewTime_ = (std::clamp)(previewTime_, 0.0f, clip_.duration);
	curveState_.currentTime = previewTime_;
	curveState_.visibleTimeMax = (std::max)(curveState_.visibleTimeMax, clip_.duration);
	curveState_.ClearSelection();
	LoadSelectedTrackEditorView();
	// 読み込み直後も現在時刻のPreviewをTarget Entityへ反映する
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipEditSession::SaveClipToSelectedAsset(const EditorToolContext& context) {

	// 保存前に表示範囲とAuto DurationをClipへ反映してからJSONへ書き出す
	clipErrorText_.clear();

	if (!clipAssetID_) {
		clipErrorText_ = "ClipData is not set.";
		return;
	}

	const AssetDatabase* database = context.toolContext.assetDatabase;
	const std::filesystem::path path = database->ResolveFullPath(clipAssetID_);
	if (path.empty()) {
		clipErrorText_ = "AnimationClip asset path was not found.";
		return;
	}

	clip_.guid = clipAssetID_;
	if (clip_.name.empty()) {
		clip_.name = path.stem().string();
	}
	StoreSelectedTrackEditorView();
	UpdateAnimationClipAutoDuration(clip_);
	clip_.duration = (std::max)(clip_.duration, 0.01f);
	for (AnimationCurveTrack& track : clip_.curveTracks) {
		// 保存時にもChannel構成を正規化し、次回ロード時のUnknown化を防ぐ
		NormalizeAnimationTrackChannels(track);
	}

	if (!SaveAnimationClipAsset(path, clip_)) {
		clipErrorText_ = "Failed to save AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err,
			"AnimationClipToolでClipを保存できません: {}", path.string());
		return;
	}

	// 保存したのでランタイム側のキャッシュを破棄し、次回再生でファイルから読み直させる
	if (SystemContext* systemContext = context.toolContext.systemContext) {
		if (systemContext->animationClipManager) {
			systemContext->animationClipManager->Invalidate(clipAssetID_);
		}
	}

	clipDirty_ = false;
}

void AnimationClipEditSession::RevertClipFromSelectedAsset(const EditorToolContext& context) {

	EndPreviewAndRestore(context);
	LoadClipFromSelectedAsset(context);
}

void AnimationClipEditSession::AddPropertyTrack(const AnimationPropertyDescriptor& desc,
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

	// 追加直後のTrackは現在値をdefaultValueへ写すだけで、キーは空のままにする
	SetupTrackInitialValue(track, currentValue);
	NormalizeAnimationTrackChannels(track);
	selectedTrackIndex_ = static_cast<int>(clip_.curveTracks.size());
	clip_.curveTracks.emplace_back(std::move(track));
	// 追加したpropertyはまだアニメで動いていないので、この時点のクリーンな現在値を基準として捕捉する
	CachePreviewBaseValues(world, entity);
	LoadSelectedTrackEditorView();
	clipDirty_ = true;
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
	const bool has3D = world->HasComponent<MeshRendererComponent>(entity) ||
		world->HasComponent<PerspectiveCameraComponent>(entity);
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

	if (editDimension_ == AnimationClipEditDimension::Mode2D ||
		editDimension_ == AnimationClipEditDimension::Mode3D) {
		return editDimension_;
	}

	const AnimationClipDetectedDimension detected = DetectTargetDimension(context);
	return detected == AnimationClipDetectedDimension::Mode2D ?
		AnimationClipEditDimension::Mode2D : AnimationClipEditDimension::Mode3D;
}

void AnimationClipEditSession::NormalizeSelectedTrackIndex() {

	if (clip_.curveTracks.empty()) {
		selectedTrackIndex_ = -1;
		return;
	}
	if (selectedTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= selectedTrackIndex_) {
		selectedTrackIndex_ = 0;
	}
}

void AnimationClipEditSession::StoreSelectedTrackEditorView() {

	if (editorViewTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= editorViewTrackIndex_) {
		return;
	}

	// Trackを切り替えても、各Propertyごとの表示範囲を保持する
	AnimationTrackEditorView& view = clip_.curveTracks[static_cast<size_t>(editorViewTrackIndex_)].editorView;
	view.timeMin = curveState_.visibleTimeMin;
	view.timeMax = curveState_.visibleTimeMax;
	view.valueMin = curveState_.visibleValueMin;
	view.valueMax = curveState_.visibleValueMax;
}

void AnimationClipEditSession::LoadSelectedTrackEditorView() {

	if (selectedTrackIndex_ < 0 || static_cast<int>(clip_.curveTracks.size()) <= selectedTrackIndex_) {
		editorViewTrackIndex_ = -1;
		return;
	}

	const AnimationTrackEditorView& view = clip_.curveTracks[static_cast<size_t>(selectedTrackIndex_)].editorView;
	curveState_.visibleTimeMin = view.timeMin;
	curveState_.visibleTimeMax = (std::max)(view.timeMax, view.timeMin + 0.001f);
	curveState_.visibleValueMin = view.valueMin;
	curveState_.visibleValueMax = (std::max)(view.valueMax, view.valueMin + 0.001f);
	// Clip編集では細かい時刻合わせが多いので、既定で1ms単位に吸着させる
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

	previewTime_ = (std::clamp)(previewTime_, 0.0f, AnimationClipEvaluator::GetPlaybackDuration(clip_));
	curveState_.currentTime = previewTime_;
}

void AnimationClipEditSession::UpdatePreviewPlayback(const EditorToolContext& context) {

	if (!previewPlaying_ || !hasClip_) {
		return;
	}

	float deltaTime = ImGui::GetIO().DeltaTime;
	if (deltaTime <= 0.0f) {
		deltaTime = context.toolContext.deltaTime;
	}

	// 再生時間を進める
	previewTime_ += deltaTime * previewSpeed_;
	// クリップデータから終了時間を取得
	float playbackDuration = AnimationClipEvaluator::GetPlaybackDuration(clip_);
	if (playbackDuration <= previewTime_) {
		// Loop時はBridge範囲も含めた再生長で折り返す
		if (clip_.loop && 0.0f < clip_.duration) {

			previewTime_ = std::fmod(previewTime_, playbackDuration);
		}
		// ループしない場合はプレビューを停止させる
		else {

			previewTime_ = playbackDuration;
			previewPlaying_ = false;
		}
	}

	SyncCurveStateTime();
	ApplyPreviewAtCurrentTime(context, false);
}

void AnimationClipEditSession::ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive) {

	if (!hasClip_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return;
	}

	// ScrubだけでもPreview扱いにして、Stop/Closeで元の値に戻せるようにする
	if (!previewActive_) {
		BeginPreview(context);
	}
	if (!previewActive_) {
		return;
	}

	AnimationClipEvaluator::ApplyClip(*world, entity, clip_, previewTime_, previewBaseValues_);
	if (!keepActive && !previewPlaying_) {
		EndPreviewAndRestore(context);
	} else {
		// Previewを継続する場合は、書き込んだ値を退避して次フレームの外部編集検知の基準にする
		CaptureLastAppliedValues(*world, entity);
	}
}

void AnimationClipEditSession::BeginPreview(const EditorToolContext& context) {

	if (previewActive_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity) || !hasClip_) {
		return;
	}

	// Target Entityへ直接値を書き込むため、開始時の値を先に退避しておく
	CachePreviewBaseValues(*world, entity);
	previewActive_ = true;
}

void AnimationClipEditSession::EndPreviewAndRestore(const EditorToolContext& context) {

	if (!previewActive_) {
		previewPlaying_ = false;
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (world && world->IsAlive(entity)) {
		// Toolを閉じた場合も、Clip編集中だけ適用していた値を元へ戻す
		RestorePreviewBaseValues(*world, entity);
	}

	// 基準値はTargetやClipが変わるまで保持する、途中でのStop/再生でクリーンな基準が汚染されないようにする
	// 適用済み値はPreview停止で無効になるので破棄する
	lastAppliedValues_.clear();
	previewActive_ = false;
	previewPlaying_ = false;
}

bool AnimationClipEditSession::HasPreviewBaseValue(const AnimationPropertyBinding& binding) const {

	for (const AnimationPreviewBaseValue& base : previewBaseValues_) {
		if (base.binding.componentName == binding.componentName &&
			base.binding.propertyPath == binding.propertyPath &&
			base.binding.valueType == binding.valueType) {
			return true;
		}
	}
	return false;
}

void AnimationClipEditSession::CachePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	// 既に捕捉済みのpropertyはクリーンな値を保持するため上書きしない、未捕捉のtrackだけ現在値から捕捉する
	// Target設定直後やProperty追加直後など、そのpropertyがアニメで動く前に呼ぶことでクリーンな基準になる
	for (const AnimationCurveTrack& track : clip_.curveTracks) {

		if (HasPreviewBaseValue(track.binding)) {
			continue;
		}

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, track.binding.componentName, track.binding.propertyPath, track.binding.valueType);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}

		// Clipに含まれるPropertyだけ退避し、無関係なComponent値は触らない
		AnimationPreviewBaseValue baseValue{};
		baseValue.binding = track.binding;
		// material override未設定などは値が無いので、復元時に除去できるよう有無を記録する
		baseValue.present = !desc->hasValue || desc->hasValue(world, entity);
		if (desc->getValue(world, entity, baseValue.value)) {
			previewBaseValues_.emplace_back(std::move(baseValue));
		}
	}

	// 向き相対クリップは基準の位置/回転が必須なので、未アニメでも捕捉しておく
	if (clip_.relativeTransform) {

		const auto ensureTransformBase = [&](const char* propertyPath, AnimationValueType valueType) {

			AnimationPropertyBinding transformBinding{};
			transformBinding.componentName = "Transform";
			transformBinding.propertyPath = propertyPath;
			transformBinding.valueType = valueType;
			if (HasPreviewBaseValue(transformBinding)) {
				return;
			}
			const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
				world, entity, "Transform", propertyPath, valueType);
			if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
				return;
			}
			AnimationPreviewBaseValue base{};
			base.binding.componentName = "Transform";
			base.binding.propertyPath = propertyPath;
			base.binding.valueType = valueType;
			base.present = !desc->hasValue || desc->hasValue(world, entity);
			if (desc->getValue(world, entity, base.value)) {
				previewBaseValues_.emplace_back(std::move(base));
			}
			};
		ensureTransformBase("localPos", AnimationValueType::Vector3);
		ensureTransformBase("localRotation", AnimationValueType::Quaternion);
		ensureTransformBase("localPos2D", AnimationValueType::Vector2);
		ensureTransformBase("localRotationZ", AnimationValueType::Float);
	}
}

void AnimationClipEditSession::RestorePreviewBaseValues(ECSWorld& world, const Entity& entity) {

	for (const AnimationPreviewBaseValue& baseValue : previewBaseValues_) {

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, baseValue.binding.componentName, baseValue.binding.propertyPath, baseValue.binding.valueType);
		if (!desc || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}

		// Preview前に値が有ったものは戻し、無かったものは除去して既定の見た目へ戻す
		if (baseValue.present) {
			if (desc->setValue) {
				desc->setValue(world, entity, baseValue.value);
			}
		} else if (desc->clearValue) {
			desc->clearValue(world, entity);
		}
	}
}

void AnimationClipEditSession::RestoreAndDropPreviewBaseValue(const EditorToolContext& context, const AnimationPropertyBinding& binding) {

	if (!previewActive_) {
		return;
	}

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity)) {
		return;
	}

	// 削除するPropertyに一致するbaseだけを元へ戻し、previewBaseValues_からも取り除く
	for (auto it = previewBaseValues_.begin(); it != previewBaseValues_.end();) {

		if (it->binding.componentName != binding.componentName ||
			it->binding.propertyPath != binding.propertyPath ||
			it->binding.valueType != binding.valueType) {
			++it;
			continue;
		}

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			*world, entity, it->binding.componentName, it->binding.propertyPath, it->binding.valueType);
		if (desc && desc->hasComponent && desc->hasComponent(*world, entity)) {

			if (it->present) {
				if (desc->setValue) {
					desc->setValue(*world, entity, it->value);
				}
			} else if (desc->clearValue) {
				desc->clearValue(*world, entity);
			}
		}
		it = previewBaseValues_.erase(it);
	}
}

void AnimationClipEditSession::CaptureLastAppliedValues(ECSWorld& world, const Entity& entity) {

	// Previewでtoolが書き込んだ直後の現在値を退避する、次フレームの外部編集検知の基準になる
	lastAppliedValues_.clear();
	for (const AnimationPreviewBaseValue& base : previewBaseValues_) {

		const std::optional<AnimationPropertyDescriptor> desc = AnimationPropertyRegistry::GetInstance().ResolveProperty(
			world, entity, base.binding.componentName, base.binding.propertyPath, base.binding.valueType);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(world, entity)) {
			continue;
		}
		AnimationPreviewBaseValue applied{};
		applied.binding = base.binding;
		if (desc->getValue(world, entity, applied.value)) {
			lastAppliedValues_.emplace_back(std::move(applied));
		}
	}
}

void AnimationClipEditSession::SyncPreviewBaseFromEntityEdits(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	const Entity entity = GetTargetEntity(context);
	if (!world || !world->IsAlive(entity) || previewBaseValues_.empty()) {
		return;
	}

	const auto resolve = [&](const AnimationPropertyBinding& binding) {
		return AnimationPropertyRegistry::GetInstance().ResolveProperty(
			*world, entity, binding.componentName, binding.propertyPath, binding.valueType);
		};
	// bindingに対応するtrackを引く、キー有無からマージ対象チャネルを判定するのに使う
	const auto findTrack = [&](const AnimationPropertyBinding& binding) -> const AnimationCurveTrack* {
		for (const AnimationCurveTrack& track : clip_.curveTracks) {
			if (track.binding.componentName == binding.componentName &&
				track.binding.propertyPath == binding.propertyPath &&
				track.binding.valueType == binding.valueType) {
				return &track;
			}
		}
		return nullptr;
		};

	if (previewActive_) {

		// toolが最後に書いた値と現在値がズレていたら、マニピュレータ/インスペクタで編集されたとみなす
		std::vector<AnimationPreviewBaseValue> edited{};
		for (const AnimationPreviewBaseValue& applied : lastAppliedValues_) {

			const std::optional<AnimationPropertyDescriptor> desc = resolve(applied.binding);
			if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(*world, entity)) {
				continue;
			}
			AnimationPropertyValue current{};
			if (!desc->getValue(*world, entity, current) || ApproxEqualValue(current, applied.value)) {
				continue;
			}
			AnimationPreviewBaseValue edit{};
			edit.binding = applied.binding;
			edit.value = current;
			edit.present = !desc->hasValue || desc->hasValue(*world, entity);
			edited.emplace_back(std::move(edit));
		}
		if (edited.empty()) {
			return;
		}

		// 編集を検知したら自動停止する、まず全プロパティをbaseへ戻し非編集分を確実に復元する
		EndPreviewAndRestore(context);
		// 編集されたプロパティはユーザー編集値を新baseとして採用し、Entityへも反映する
		for (const AnimationPreviewBaseValue& edit : edited) {

			AnimationPreviewBaseValue* baseEntry = nullptr;
			for (AnimationPreviewBaseValue& base : previewBaseValues_) {
				if (base.binding.componentName == edit.binding.componentName &&
					base.binding.propertyPath == edit.binding.propertyPath &&
					base.binding.valueType == edit.binding.valueType) {
					baseEntry = &base;
					break;
				}
			}
			// キーのあるチャネル(=アニメ軸)はbaseを保ち、キーの無いチャネルだけ編集値を採用する
			const AnimationCurveTrack* track = findTrack(edit.binding);
			const AnimationPropertyValue merged = (baseEntry && track) ?
				MergeEditedBaseValue(*track, edit.value, baseEntry->value) : edit.value;

			const std::optional<AnimationPropertyDescriptor> desc = resolve(edit.binding);
			if (desc && desc->setValue) {
				desc->setValue(*world, entity, merged);
			}
			if (baseEntry) {
				baseEntry->value = merged;
				baseEntry->present = edit.present;
			}
		}
		return;
	}

	// 停止中: baseとズレていたらユーザー編集なのでbaseを更新する
	for (AnimationPreviewBaseValue& base : previewBaseValues_) {

		const std::optional<AnimationPropertyDescriptor> desc = resolve(base.binding);
		if (!desc || !desc->getValue || !desc->hasComponent || !desc->hasComponent(*world, entity)) {
			continue;
		}
		AnimationPropertyValue current{};
		if (!desc->getValue(*world, entity, current) || ApproxEqualValue(current, base.value)) {
			continue;
		}
		// キーのあるチャネルはbaseを保ち、キーの無いチャネルだけ編集値を採用する
		const AnimationCurveTrack* track = findTrack(base.binding);
		base.value = track ? MergeEditedBaseValue(*track, current, base.value) : current;
		base.present = !desc->hasValue || desc->hasValue(*world, entity);
	}
}

void AnimationClipEditSession::AddKeyToChannel(AnimationCurveTrack& track, uint32_t channelIndex, float time) {

	if (track.channels.size() <= channelIndex) {
		return;
	}

	CurveChannel& channel = track.channels[channelIndex];
	const float value = channel.Evaluate(time);
	constexpr float kSameTimeEpsilon = 0.0005f;
	for (CurveKey& key : channel.keys) {
		if (std::abs(key.time - time) <= kSameTimeEpsilon) {
			// ほぼ同時刻のキーは増やさず、現在の評価値で上書きする
			key.time = time;
			key.value = value;
			return;
		}
	}

	const uint32_t addedIndex = channel.AddKey(time, value, CurveInterpolationMode::Spline);
	if (IsQuaternionAxisAngleTrack(track) && channelIndex == 0u) {
		CurveQuaternionAxisKey axisKey = QuaternionAxisKeyUtility::MakeDefault();
		if (!track.quaternionAxisKeys.empty()) {
			const uint32_t sourceIndex = (std::min)(
				addedIndex,
				static_cast<uint32_t>(track.quaternionAxisKeys.size() - 1));
			axisKey = track.quaternionAxisKeys[sourceIndex];
		}
		const uint32_t insertIndex = (std::min)(addedIndex, static_cast<uint32_t>(track.quaternionAxisKeys.size()));
		track.quaternionAxisKeys.insert(track.quaternionAxisKeys.begin() + insertIndex, axisKey);
		SortQuaternionAxisKeys(track);
	}
}

void AnimationClipEditSession::UpdateAutoDurationAndPreview(const EditorToolContext& context) {

	// キー追加/生成後はDuration、現在時刻、SceneView Previewをまとめて同期する
	UpdateAnimationClipAutoDuration(clip_);
	previewTime_ = (std::clamp)(previewTime_, 0.0f, AnimationClipEvaluator::GetPlaybackDuration(clip_));
	curveState_.currentTime = previewTime_;
	clipDirty_ = true;
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipEditSession::UpdateExternalEdits(const EditorToolContext& context) {

	if (context.panelContext && context.panelContext->editorState) {

		const EditorState& editorState = *context.panelContext->editorState;
		const size_t undoCount = editorState.commandHistory.GetUndoCount();
		const size_t redoCount = editorState.commandHistory.GetRedoCount();
		if (editorState.useSceneGizmo || undoCount != lastUndoCount_ || redoCount != lastRedoCount_) {
			SyncPreviewBaseFromEntityEdits(context);
		}
		lastUndoCount_ = undoCount;
		lastRedoCount_ = redoCount;
	}
}

void AnimationClipEditSession::SetPreviewTarget(const EditorToolContext& context, const UUID& nextTargetUUID) {

	// 旧対象の値を復元してから新対象へ現在時刻の値を適用する
	EndPreviewAndRestore(context);
	previewBaseValues_.clear();
	targetEntityUUID_ = nextTargetUUID;
	previewTime_ = hasClip_ ? (std::clamp)(previewTime_, 0.0f, clip_.duration) : 0.0f;
	curveState_.currentTime = previewTime_;
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipEditSession::ClearPreviewTarget(const EditorToolContext& context) {

	// 編集前の値を復元してから対象を解除する
	EndPreviewAndRestore(context);
	previewBaseValues_.clear();
	targetEntityUUID_ = {};
}

void AnimationClipEditSession::TogglePreviewPlayback(const EditorToolContext& context) {

	if (previewPlaying_) {
		previewPlaying_ = false;
	} else {
		// 終端でPlayした時は、AnimationClipの先頭から再生し直す
		if (clip_.duration <= previewTime_) {
			previewTime_ = 0.0f;
			curveState_.currentTime = previewTime_;
		}
		BeginPreview(context);
		previewPlaying_ = previewActive_;
		ApplyPreviewAtCurrentTime(context, true);
	}
}

void AnimationClipEditSession::StopPreviewPlayback(const EditorToolContext& context) {

	// StopはPreviewを解除し、編集前のEntity値へ戻したうえで時刻を先頭へ戻す
	EndPreviewAndRestore(context);
	previewTime_ = 0.0f;
	curveState_.currentTime = previewTime_;
}
