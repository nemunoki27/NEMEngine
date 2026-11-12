#include "AnimationClipEditSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>

// c++
#include <algorithm>

using namespace Engine;

void AnimationClipEditSession::LoadClipFromSelectedAsset(const EditorToolContext& context) {

	clipErrorText_.clear();
	if (!clipAssetID_) {
		EndPreviewAndRestore();
		hasClip_ = false;
		loadedClipAssetID_ = {};
		previewBaseValues_.clear();
		document_ = std::make_shared<AnimationClipDocument>();
		return;
	}
	const auto store = GetAnimationClipDocumentStore(context);
	const auto document = store ? store->Acquire(*context.toolContext.assetDatabase, clipAssetID_, clipErrorText_) : nullptr;
	if (!document) { clipAssetID_ = loadedClipAssetID_; return; }
	AttachDocument(document);
	ApplyPreviewAtCurrentTime(context, true);
}

void AnimationClipEditSession::SaveClipToSelectedAsset(const EditorToolContext& context) {

	clipErrorText_.clear();
	if (!hasClip_ || !clipAssetID_) {
		clipErrorText_ = "アニメーションクリップが設定されていません";
		return;
	}
	StoreSelectedTrackEditorView();
	UpdateAnimationClipAutoDuration(GetClip());
	GetClip().duration = (std::max)(GetClip().duration, 0.01f);
	for (auto& track : GetClip().curveTracks) NormalizeAnimationTrackChannels(track);
	auto* manager = context.toolContext.systemContext ? context.toolContext.systemContext->animationClipManager : nullptr;
	if (!document_->Save(*context.toolContext.assetDatabase, manager)) clipErrorText_ = document_->GetError();
}

void AnimationClipEditSession::RevertClipFromSelectedAsset(const EditorToolContext& context) {

	EndPreviewAndRestore();
	document_->Discard();
	NormalizeSelectedTrackIndex();
	curveState_.ClearSelection();
	editorViewTrackIndex_ = -1;
	ApplyPreviewAtCurrentTime(context, true);
}

bool AnimationClipEditSession::CreateClip(const EditorToolContext& context, const std::string& name) {

	const AssetID assetID = ProjectAssetFileUtility::CreateRegisteredAsset(*context.toolContext.assetDatabase,
		"GameAssets", ProjectAssetFileKind::AnimationClip, name, clipErrorText_);
	if (!assetID) return false;
	clipAssetID_ = assetID;
	LoadClipFromSelectedAsset(context);
	return loadedClipAssetID_ == assetID && clipErrorText_.empty();
}

bool AnimationClipEditSession::ImportSettings(const EditorToolContext& context, AssetID sourceAsset) {

	const AssetDatabase& database = *context.toolContext.assetDatabase;
	const auto* meta = database.Find(sourceAsset);
	AnimationClipAsset loaded;
	if (!hasClip_ || !clipAssetID_ || sourceAsset == clipAssetID_ || !meta || meta->type != AssetType::AnimationClip ||
		!LoadAnimationClipAsset(database.ResolveFullPath(sourceAsset), loaded)) {
		clipErrorText_ = "取り込み元のアニメーションクリップを読み込めません";
		return false;
	}
	EndPreviewAndRestore();
	loaded.name = GetClip().name;
	loaded.guid = clipAssetID_;
	GetClip() = std::move(loaded);
	MarkClipDirty();
	CommitHistory();
	NormalizeSelectedTrackIndex();
	curveState_.ClearSelection();
	ApplyPreviewAtCurrentTime(context, true);
	clipErrorText_.clear();
	return true;
}
