#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineAsset.h>
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>
#include <Engine/Editor/Tools/Core/EditorToolContext.h>

// c++
#include <deque>
#include <memory>

namespace Engine {

	struct TimelineEditorPreviewState;

	//============================================================================
	//	TimelineEditorSession class
	//	アセット編集履歴と隔離した再生状態を所有する
	//============================================================================
	class TimelineEditorSession {
	public:
		TimelineEditorSession() = default;
		~TimelineEditorSession();
		TimelineEditorSession(const TimelineEditorSession&) = delete;
		TimelineEditorSession& operator=(const TimelineEditorSession&) = delete;

		bool Load(const EditorToolContext& context, AssetID assetID);
		bool Create(const EditorToolContext& context, const std::string& name);
		bool Import(const EditorToolContext& context, AssetID assetID);
		bool Save(const EditorToolContext& context);
		void SyncExternalAsset(const EditorToolContext& context, bool allowReload);
		void DiscardChanges(const EditorToolContext& context);
		void CommitHistory();
		void RestoreHistory(bool redo);
		void SetOwner(const EditorToolContext& context, UUID owner);
		bool SaveBindings(const EditorToolContext& context);
		void UpdatePreview(const EditorToolContext& context, double deltaTime);
		void EvaluatePreview(const EditorToolContext& context);
		void EndPreview();
		// 再生操作をプレビューと実行中のPlayerへ振り分ける
		void TogglePlayback(const EditorToolContext& context);
		void StopPlayback(const EditorToolContext& context);
		void Seek(const EditorToolContext& context, double value);
		void Step(const EditorToolContext& context, int frames);
		void SyncPlayback(const EditorToolContext& context);
		// 変更した世代だけをプレビューと実行へ公開する
		void MarkChanged();
		void PublishDraft(const EditorToolContext& context);
		uint64_t GetDraftRevision() const { return draftRevision_; }

		//--------- accessor -----------------------------------------------------

		TimelineAsset& GetAsset() { return asset_; }
		const TimelineAsset& GetAsset() const { return asset_; }
		TimelinePlayerSettings& GetPreviewSettings() { return previewSettings_; }
		const TimelinePlayerSettings& GetPreviewSettings() const { return previewSettings_; }
		AssetID GetAssetID() const { return assetID_; }
		UUID GetOwnerID() const { return ownerID_; }
		Entity GetOwner(const EditorToolContext& context) const;
		bool HasAsset() const { return hasAsset_; }
		bool IsPreviewActive() const { return static_cast<bool>(preview_); }
		bool IsDirty() const { return dirty_; }
		const std::string& GetError() const { return error_; }
		const std::string& GetPreviewDiagnostic() const;
		float GetPreviewProgress() const;
		std::deque<TimelineAsset>& GetUndo() { return undo_; }
		std::deque<TimelineAsset>& GetRedo() { return redo_; }
		uint64_t GetUndoSerial() const { return undo_.empty() ? 0 : currentSerial_; }
		uint64_t GetRedoSerial() const;

		double time = 0.0;
		double speed = 1.0;
		bool playing = false;
		bool previewEnabled = true;
		bool loop = false;
		bool rangeEnabled = false;
		double rangeStart = 0.0;
		double rangeEnd = 1.0;

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		AssetID assetID_{};
		TimelineAsset asset_{};
		TimelinePlayerSettings previewSettings_{};
		UUID ownerID_{};
		SceneEntityReference ownerReference_{};
		UUID ownerSceneInstanceID_{};
		bool hasAsset_ = false;
		bool dirty_ = false;
		std::string error_;
		std::string savedState_;
		std::string historyState_;
		TimelineAsset historyCurrent_{};
		TimelineAsset savedAsset_{};
		uint64_t assetRevision_ = 0;
		bool sourceMissing_ = false;
		std::deque<TimelineAsset> undo_;
		std::deque<TimelineAsset> redo_;
		std::deque<uint64_t> undoSerials_, redoSerials_;
		uint64_t currentSerial_ = 0, localSerial_ = 0, redoEpoch_ = 0;
		std::shared_ptr<class AnimationClipDocumentStore> clipDocuments_;
		std::shared_ptr<TimelineEditorPreviewState> preview_;
		uint64_t draftRevision_ = 1;
		uint64_t historyRevision_ = 0;
		uint64_t publishedRevision_ = 0;
		std::weak_ptr<const ECSWorldLifetime> publishedWorld_;
		std::string draftError_;
		bool scrubbing_ = false;
		bool seekPending_ = false;
		bool resumeAfterSeek_ = false;
		std::weak_ptr<const ECSWorldLifetime> seekWorld_;

		// 読み込んだ編集値と履歴を差し替える
		void ApplyLoaded(TimelineAsset asset, AssetID assetID, bool dirty);
	};
}
