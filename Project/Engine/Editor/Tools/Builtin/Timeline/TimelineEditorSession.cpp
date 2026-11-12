#include "TimelineEditorSession.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/Animation/Timeline/TimelinePlayerAccess.h>
#include <Engine/Editor/Assets/Project/ProjectAssetFileUtility.h>
#include <Engine/Editor/Commands/Components/SetSerializedComponentCommand.h>
#include <Engine/Editor/UI/Panels/Core/IEditorPanelHost.h>
#include <Engine/Editor/Animation/AnimationClipDocument.h>

// c++
#include <algorithm>

using namespace Engine;

//============================================================================
//	TimelineEditorSession methods
//============================================================================
bool TimelineEditorSession::Load(const EditorToolContext& context, AssetID assetID) {

	clipDocuments_ = GetAnimationClipDocumentStore(context);
	TimelineAsset loaded;
	if (!context.toolContext.assetDatabase || !assetID ||
		!LoadTimelineAsset(context.toolContext.assetDatabase->ResolveFullPath(assetID), loaded, &error_) ||
		!ValidateTimelineAsset(loaded, error_)) return false;
	ApplyLoaded(std::move(loaded), assetID, false);
	assetRevision_ = context.toolContext.assetDatabase->GetContentRevision(assetID_);
	SetOwner(context, ownerID_);
	return true;
}

bool TimelineEditorSession::Create(const EditorToolContext& context, const std::string& name) {

	if (!context.toolContext.assetDatabase) return false;
	const AssetID assetID = ProjectAssetFileUtility::CreateRegisteredAsset(*context.toolContext.assetDatabase,
		"GameAssets", ProjectAssetFileKind::Timeline, name, error_);
	return assetID && Load(context, assetID);
}

bool TimelineEditorSession::Import(const EditorToolContext& context, AssetID assetID) {

	if (!hasAsset_ || !context.toolContext.assetDatabase || assetID == assetID_) return false;
	TimelineAsset loaded;
	const auto* meta = context.toolContext.assetDatabase->Find(assetID);
	if (!meta || meta->type != AssetType::Timeline ||
		!LoadTimelineAsset(context.toolContext.assetDatabase->ResolveFullPath(assetID), loaded, &error_) ||
		!ValidateTimelineAsset(loaded, error_)) return false;
	EndPreview();
	loaded.guid = assetID_;
	loaded.name = asset_.name;
	asset_ = std::move(loaded);
	MarkChanged();
	SetOwner(context, ownerID_);
	CommitHistory();
	return true;
}

bool TimelineEditorSession::Save(const EditorToolContext& context) {

	if (!hasAsset_ || !context.toolContext.assetDatabase) return false;
	asset_.guid = assetID_;
	if (!SaveTimelineAsset(context.toolContext.assetDatabase->ResolveFullPath(assetID_), asset_, &error_)) return false;
	context.toolContext.assetDatabase->NotifyContentChanged(assetID_);
	assetRevision_ = context.toolContext.assetDatabase->GetContentRevision(assetID_);
	savedAsset_ = asset_;
	savedState_ = nlohmann::json(asset_).dump();
	dirty_ = false;
	sourceMissing_ = false;
	return true;
}

void TimelineEditorSession::SyncExternalAsset(const EditorToolContext& context, bool allowReload) {

	if (!hasAsset_ || !context.toolContext.assetDatabase) return;
	const auto* database = context.toolContext.assetDatabase;
	if (!database->Find(assetID_)) {
		EndPreview();
		sourceMissing_ = true;
		assetRevision_ = ~uint64_t{};
		error_ = "Timelineアセットが削除されました";
		return;
	}
	const uint64_t revision = database->GetContentRevision(assetID_);
	if (revision == assetRevision_) return;
	if (dirty_ || !allowReload) {
		error_ = "ディスク上のTimelineが変更されています 現在の編集を保存するか破棄してください";
		return;
	}
	EndPreview();
	assetRevision_ = revision;
	sourceMissing_ = !Load(context, assetID_);
}

void TimelineEditorSession::DiscardChanges(const EditorToolContext& context) {

	const bool missing = sourceMissing_;
	ApplyLoaded(savedAsset_, assetID_, false);
	SetOwner(context, ownerID_);
	sourceMissing_ = missing;
	if (missing) error_ = "Timelineアセットが削除されました";
}

void TimelineEditorSession::ApplyLoaded(TimelineAsset asset, AssetID assetID, bool dirty) {

	EndPreview();
	assetID_ = assetID;
	scrubbing_ = false;
	seekPending_ = false;
	resumeAfterSeek_ = false;
	asset_ = std::move(asset);
	asset_.guid = assetID_;
	hasAsset_ = true;
	dirty_ = dirty;
	error_.clear();
	undo_.clear();
	redo_.clear();
	undoSerials_.clear();
	redoSerials_.clear();
	currentSerial_ = 0;
	historyCurrent_ = asset_;
	savedAsset_ = asset_;
	historyState_ = nlohmann::json(asset_).dump();
	savedState_ = dirty ? std::string{} : historyState_;
	time = (std::clamp)(time, 0.0, asset_.duration);
	previewSettings_.timeline = assetID_;
	sourceMissing_ = false;
	++draftRevision_;
	historyRevision_ = draftRevision_;
	rangeStart = 0.0;
	rangeEnd = asset_.duration;
}

void TimelineEditorSession::CommitHistory() {

	if (!hasAsset_ || historyRevision_ == draftRevision_) return;
	const std::string state = nlohmann::json(asset_).dump();
	historyRevision_ = draftRevision_;
	if (state == historyState_) { dirty_ = state != savedState_; return; }
	undo_.push_back(historyCurrent_);
	undoSerials_.push_back(currentSerial_);
	if (undo_.size() > 128) { undo_.pop_front(); undoSerials_.pop_front(); }
	redo_.clear();
	redoSerials_.clear();
	currentSerial_ = clipDocuments_ ? clipDocuments_->NextEditSerial() : ++localSerial_;
	historyCurrent_ = asset_;
	historyState_ = state;
	dirty_ = state != savedState_;
}

void TimelineEditorSession::RestoreHistory(bool redo) {

	CommitHistory();
	if (clipDocuments_) {
		const auto sourceSerial = redo ? clipDocuments_->GetRedoSerial() : clipDocuments_->GetUndoSerial();
		const auto ownSerial = redo ? GetRedoSerial() : GetUndoSerial();
		if (sourceSerial && (!ownSerial || (redo ? sourceSerial < ownSerial : sourceSerial > ownSerial))) {
			clipDocuments_->RestoreHistory(redo);
			return;
		}
	}
	auto& from = redo ? redo_ : undo_;
	auto& to = redo ? undo_ : redo_;
	auto& fromSerials = redo ? redoSerials_ : undoSerials_;
	auto& toSerials = redo ? undoSerials_ : redoSerials_;
	if (from.empty() || (redo && !GetRedoSerial())) return;
	EndPreview();
	to.push_back(asset_);
	toSerials.push_back(currentSerial_);
	asset_ = std::move(from.back());
	currentSerial_ = fromSerials.back();
	fromSerials.pop_back();
	from.pop_back();
	redoEpoch_ = clipDocuments_ ? clipDocuments_->GetHistoryEpoch() : 0;
	historyCurrent_ = asset_;
	historyState_ = nlohmann::json(asset_).dump();
	dirty_ = historyState_ != savedState_;
	time = (std::clamp)(time, 0.0, asset_.duration);
	++draftRevision_;
	historyRevision_ = draftRevision_;
}

uint64_t TimelineEditorSession::GetRedoSerial() const {

	return !redo_.empty() && (!clipDocuments_ || redoEpoch_ == clipDocuments_->GetHistoryEpoch()) ? redoSerials_.back() : 0;
}

Entity TimelineEditorSession::GetOwner(const EditorToolContext& context) const {

	ECSWorld* world = context.GetWorld();
	if (!world || !ownerID_) return Entity::Null();
	const Entity direct = world->FindByUUID(ownerID_);
	if (world->IsAlive(direct)) return direct;
	if (!ownerSceneInstanceID_ || !ownerReference_.localFileID) return Entity::Null();
	const Entity target = SceneObjectUtility::FindByLocalFileID(*world, ownerSceneInstanceID_, ownerReference_.localFileID);
	const auto* scene = world->TryGetComponent<SceneObjectComponent>(target);
	return scene && scene->sourceAsset == ownerReference_.sourceAsset ? target : Entity::Null();
}

void TimelineEditorSession::SetOwner(const EditorToolContext& context, UUID owner) {

	EndPreview();
	scrubbing_ = false;
	seekPending_ = false;
	resumeAfterSeek_ = false;
	const bool changedOwner = ownerID_ != owner;
	ownerID_ = owner;
	if (changedOwner) { ownerReference_ = {}; ownerSceneInstanceID_ = {}; }
	ECSWorld* world = context.GetWorld();
	const Entity entity = GetOwner(context);
	if (!world || !world->IsAlive(entity)) {
		previewSettings_ = {};
		previewSettings_.timeline = assetID_;
		return;
	}
	ownerReference_ = SceneEntityReference::FromEntity(*world, entity);
	ownerSceneInstanceID_ = SceneObjectUtility::GetSceneInstanceID(*world, entity);
	bool copiedPlayer = false;
	if (const auto* player = TryGetTimelinePlayer(*world, entity); player && player->settings.timeline == assetID_) {
		previewSettings_ = player->settings;
		copiedPlayer = true;
	}
	if (changedOwner && !copiedPlayer) previewSettings_ = TimelinePlayerSettings{};
	previewSettings_.timeline = assetID_;
	std::erase_if(previewSettings_.bindings, [&](const auto& binding) {
		return std::none_of(asset_.bindings.begin(), asset_.bindings.end(), [&](const auto& definition) { return definition.id == binding.slot; });
	});
	std::erase_if(previewSettings_.parameters, [&](const auto& parameter) {
		return std::none_of(asset_.parameters.begin(), asset_.parameters.end(), [&](const auto& definition) { return definition.id == parameter.parameter; });
	});
	for (const auto& definition : asset_.bindings) {
		auto found = std::find_if(previewSettings_.bindings.begin(), previewSettings_.bindings.end(),
			[&](const auto& binding) { return binding.slot == definition.id; });
		if (found != previewSettings_.bindings.end()) continue;
		TimelinePlayerBinding binding;
		binding.slot = definition.id;
		if (definition.name == "Actor" || asset_.bindings.size() == 1) {
			if (HasTimelineBindingComponent(*world, entity, definition.component)) {
				binding.target = SceneEntityReference::FromEntity(*world, entity);
				binding.runtimeTargetID = binding.target.localFileID ? UUID{} : world->GetUUID(entity);
			}
		}
		previewSettings_.bindings.push_back(binding);
	}
}

bool TimelineEditorSession::SaveBindings(const EditorToolContext& context) {

	ECSWorld* world = context.GetWorld();
	const Entity owner = GetOwner(context);
	if (!world || !world->IsAlive(owner) || !context.panelContext || !context.panelContext->host) return false;
	const auto* player = TryGetTimelinePlayer(*world, owner);
	if (!player) { error_ = "対象にTimelinePlayerコンポーネントを追加してください"; return false; }
	auto next = player->settings;
	next.bindings = previewSettings_.bindings;
	next.parameters = previewSettings_.parameters;
	next.timeline = assetID_;
	const bool success = context.panelContext->host->ExecuteEditorCommand(std::make_unique<SetSerializedComponentCommand>(
		owner, "TimelinePlayer", nlohmann::json(player->settings), nlohmann::json(next)));
	if (success) error_.clear();
	if (success) ++draftRevision_;
	return success;
}
