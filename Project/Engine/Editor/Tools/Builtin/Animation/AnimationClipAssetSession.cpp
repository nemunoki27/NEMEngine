#include "AnimationClipEditSession.h"
#include "AnimationClipEditorUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>

// c++
#include <algorithm>
#include <cmath>
#include <utility>

using namespace Engine;
using namespace Engine::AnimationClipEditorUtility;

//============================================================================
//	AnimationClipEditSession
//============================================================================

void AnimationClipEditSession::LoadClipFromSelectedAsset(const EditorToolContext& context) {

	// 読込に成功するまで現在の編集値を保持する
	clipErrorText_.clear();

	if (!clipAssetID_) {
		EndPreviewAndRestore();
		hasClip_ = false;
		clipDirty_ = false;
		loadedClipAssetID_ = {};
		previewBaseValues_.clear();
		clip_ = AnimationClipAsset{};
		return;
	}

	const AssetDatabase* database = context.toolContext.assetDatabase;
	const std::filesystem::path path = database->ResolveFullPath(clipAssetID_);
	if (path.empty()) {
		clipErrorText_ = "AnimationClip asset path was not found.";
		clipAssetID_ = loadedClipAssetID_;
		return;
	}

	AnimationClipAsset loaded{};
	if (!LoadAnimationClipAsset(path, loaded)) {
		clipAssetID_ = loadedClipAssetID_;
		clipErrorText_ = "Failed to load AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err, "AnimationClipToolでClipを読み込めません: {}", path.string());
		return;
	}

	EndPreviewAndRestore();
	previewBaseValues_.clear();
	clipDirty_ = false;
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

	// 保存する表示範囲と再生時間を揃える
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
		// 保存するChannel構成を正規化する
		NormalizeAnimationTrackChannels(track);
	}

	if (!SaveAnimationClipAsset(path, clip_)) {
		clipErrorText_ = "Failed to save AnimationClip asset.";
		Logger::Output(LogType::Engine, spdlog::level::err, "AnimationClipToolでClipを保存できません: {}", path.string());
		return;
	}

	// 保存したClipの実行cacheを失効させる
	if (SystemContext* systemContext = context.toolContext.systemContext) {
		if (systemContext->animationClipManager) {
			systemContext->animationClipManager->Invalidate(clipAssetID_);
		}
	}

	context.toolContext.assetDatabase->NotifyContentChanged(clipAssetID_);
	clipDirty_ = false;
}

void AnimationClipEditSession::RevertClipFromSelectedAsset(const EditorToolContext& context) {

	EndPreviewAndRestore(context);
	LoadClipFromSelectedAsset(context);
}
