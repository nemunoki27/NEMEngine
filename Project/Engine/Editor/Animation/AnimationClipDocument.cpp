#include "AnimationClipDocument.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAssetOverride.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

// c++
#include <algorithm>

using namespace Engine;

void AnimationClipDocument::Reset(AnimationClipAsset clip, bool dirty) {

	clip_ = std::move(clip);
	clip_.duration = (std::max)(clip_.duration, 0.01f);
	for (auto& track : clip_.curveTracks) NormalizeAnimationTrackChannels(track);
	saved_ = clip_;
	historyCurrent_ = clip_;
	historyState_ = nlohmann::json(clip_).dump();
	savedState_ = dirty ? std::string{} : historyState_;
	undo_.clear();
	redo_.clear();
	error_.clear();
	dirty_ = dirty;
	historyPending_ = false;
	currentSerial_ = 0;
	snapshot_.reset();
	++revision_;
}

void AnimationClipDocument::MarkDirty() {

	dirty_ = true;
	historyPending_ = true;
	snapshot_.reset();
	++revision_;
}

void AnimationClipDocument::CommitHistory() {

	if (!historyPending_) return;
	historyPending_ = false;
	const std::string state = nlohmann::json(clip_).dump();
	if (state != historyState_) {
		undo_.push_back({historyCurrent_, currentSerial_, historyClock_->epoch});
		if (undo_.size() > 128) undo_.pop_front();
		redo_.clear();
		historyCurrent_ = clip_;
		historyState_ = state;
		currentSerial_ = ++historyClock_->serial;
		++historyClock_->epoch;
	}
	dirty_ = state != savedState_;
}

bool AnimationClipDocument::RestoreHistory(bool redo) {

	CommitHistory();
	auto& from = redo ? redo_ : undo_;
	auto& to = redo ? undo_ : redo_;
	if (from.empty() || (redo && !GetRedoSerial())) return false;
	to.push_back({clip_, currentSerial_, historyClock_->epoch});
	clip_ = std::move(from.back().value);
	currentSerial_ = from.back().serial;
	from.pop_back();
	historyCurrent_ = clip_;
	historyState_ = nlohmann::json(clip_).dump();
	dirty_ = historyState_ != savedState_;
	snapshot_.reset();
	++revision_;
	return true;
}

uint64_t AnimationClipDocument::GetRedoSerial() const {

	return !redo_.empty() && redo_.back().epoch == historyClock_->epoch ? redo_.back().serial : 0;
}

bool AnimationClipDocument::Save(AssetDatabase& database, AnimationClipManager* manager) {

	CommitHistory();
	error_.clear();
	const auto path = database.ResolveFullPath(clip_.guid);
	if (path.empty() || !SaveAnimationClipAsset(path, clip_)) {
		error_ = "アニメーションクリップを保存できません";
		return false;
	}
	database.NotifyContentChanged(clip_.guid);
	diskRevision_ = database.GetContentRevision(clip_.guid);
	if (manager) manager->Invalidate(clip_.guid);
	saved_ = clip_;
	savedState_ = nlohmann::json(clip_).dump();
	dirty_ = false;
	return true;
}

void AnimationClipDocument::Discard() {

	const auto saved = saved_;
	Reset(saved);
}

void AnimationClipDocument::SyncExternal(AssetDatabase& database) {

	const auto revision = database.GetContentRevision(clip_.guid);
	if (diskRevision_ == UINT64_MAX) { diskRevision_ = revision; return; }
	if (revision == diskRevision_) return;
	if (dirty_) {
		error_ = "保存内容が変更されています 編集を保存するか破棄してください";
		return;
	}
	AnimationClipAsset loaded;
	if (!LoadAnimationClipAsset(database.ResolveFullPath(clip_.guid), loaded)) {
		error_ = "アニメーションクリップを読み込めません";
		return;
	}
	loaded.guid = clip_.guid;
	Reset(std::move(loaded));
	diskRevision_ = revision;
}

std::shared_ptr<const AnimationClipAsset> AnimationClipDocument::GetSnapshot() {

	if (!snapshot_) snapshot_ = std::make_shared<const AnimationClipAsset>(clip_);
	return snapshot_;
}

std::shared_ptr<AnimationClipDocument> AnimationClipDocumentStore::Acquire(
	AssetDatabase& database, AssetID asset, std::string& error) {

	if (auto document = Find(asset)) {
		document->SyncExternal(database);
		error = document->GetError();
		return document;
	}
	AnimationClipAsset clip;
	if (!asset || !LoadAnimationClipAsset(database.ResolveFullPath(asset), clip)) {
		error = "アニメーションクリップを読み込めません";
		return {};
	}
	clip.guid = asset;
	auto document = std::make_shared<AnimationClipDocument>();
	document->SetHistoryClock(historyClock_);
	document->Reset(std::move(clip));
	document->SyncExternal(database);
	documents_.emplace(asset, document);
	error.clear();
	return document;
}

std::shared_ptr<AnimationClipDocument> AnimationClipDocumentStore::Find(AssetID asset) const {

	const auto found = documents_.find(asset);
	return found == documents_.end() ? nullptr : found->second;
}

void AnimationClipDocumentStore::Publish(ECSWorld& world) {

	for (const auto& [asset, document] : documents_) {
		const auto snapshot = document->GetSnapshot();
		if (GetAnimationClipAssetOverride(world, asset) != snapshot)
			PublishAnimationClipAssetOverride(world, asset, snapshot);
	}
}

bool AnimationClipDocumentStore::SaveDirty(AssetDatabase& database, AnimationClipManager* manager) {

	for (const auto& [asset, document] : documents_)
		if (document->IsDirty() && !document->Save(database, manager)) return false;
	return true;
}

void AnimationClipDocumentStore::DiscardDirty() {

	for (const auto& [asset, document] : documents_) if (document->IsDirty()) document->Discard();
}

bool AnimationClipDocumentStore::HasDirty() const {

	return std::ranges::any_of(documents_, [](const auto& item) { return item.second->IsDirty(); });
}

void AnimationClipDocumentStore::SyncExternal(AssetDatabase& database) {

	for (const auto& [asset, document] : documents_) document->SyncExternal(database);
}

uint64_t AnimationClipDocumentStore::NextEditSerial() {

	++historyClock_->epoch;
	return ++historyClock_->serial;
}

uint64_t AnimationClipDocumentStore::GetUndoSerial() const {

	uint64_t serial = 0;
	for (const auto& [asset, document] : documents_) serial = (std::max)(serial, document->GetUndoSerial());
	return serial;
}

uint64_t AnimationClipDocumentStore::GetRedoSerial() const {

	uint64_t serial = 0;
	for (const auto& [asset, document] : documents_) {
		const auto next = document->GetRedoSerial();
		if (next && (!serial || next < serial)) serial = next;
	}
	return serial;
}

bool AnimationClipDocumentStore::RestoreHistory(bool redo) {

	const auto serial = redo ? GetRedoSerial() : GetUndoSerial();
	if (!serial) return false;
	for (const auto& [asset, document] : documents_)
		if ((redo ? document->GetRedoSerial() : document->GetUndoSerial()) == serial) return document->RestoreHistory(redo);
	return false;
}

std::shared_ptr<AnimationClipDocumentStore> Engine::GetAnimationClipDocumentStore(const EditorToolContext& context) {

	if (!context.panelContext || !context.panelContext->editorState) return {};
	auto& store = context.panelContext->editorState->animationClipDocuments;
	if (!store) store = std::make_shared<AnimationClipDocumentStore>();
	if (context.toolContext.assetDatabase) store->SyncExternal(*context.toolContext.assetDatabase);
	return store;
}
