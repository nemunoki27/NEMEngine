#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>

// c++
#include <deque>
#include <memory>
#include <unordered_map>
#include <utility>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	class AnimationClipManager;
	struct EditorToolContext;

	struct AnimationEditHistoryClock {

		uint64_t serial = 0;
		uint64_t epoch = 0;
	};

	//============================================================================
	//	AnimationClipDocument class
	//	複数ツールで同じアセットの編集値と履歴を共有する
	//============================================================================
	class AnimationClipDocument {
	public:
		// 読み込んだ内容を新しい編集基準にする
		void Reset(AnimationClipAsset clip, bool dirty = false);
		// 値の変更を次の履歴へまとめる
		void MarkDirty();
		void CommitHistory();
		bool RestoreHistory(bool redo);
		void SetHistoryClock(std::shared_ptr<AnimationEditHistoryClock> clock) { historyClock_ = std::move(clock); }
		// 編集値を保存して実行キャッシュを失効させる
		bool Save(AssetDatabase& database, AnimationClipManager* manager);
		// 保存済みの内容へ戻す
		void Discard();
		// 外部で変更された保存内容を安全に取り込む
		void SyncExternal(AssetDatabase& database);

		//--------- accessor -----------------------------------------------------

		AnimationClipAsset& GetClip() { return clip_; }
		const AnimationClipAsset& GetClip() const { return clip_; }
		bool IsDirty() const { return dirty_; }
		uint64_t GetRevision() const { return revision_; }
		uint64_t GetUndoSerial() const { return undo_.empty() ? 0 : currentSerial_; }
		uint64_t GetRedoSerial() const;
		const std::string& GetError() const { return error_; }
		std::shared_ptr<const AnimationClipAsset> GetSnapshot();

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- structure ----------------------------------------------------

		struct HistoryEntry {

			AnimationClipAsset value;
			uint64_t serial = 0, epoch = 0;
		};

		//--------- variables ----------------------------------------------------

		AnimationClipAsset clip_{};
		AnimationClipAsset saved_{};
		AnimationClipAsset historyCurrent_{};
		std::string savedState_;
		std::string historyState_;
		std::string error_;
		std::deque<HistoryEntry> undo_;
		std::deque<HistoryEntry> redo_;
		std::shared_ptr<AnimationEditHistoryClock> historyClock_ = std::make_shared<AnimationEditHistoryClock>();
		uint64_t currentSerial_ = 0;
		std::shared_ptr<const AnimationClipAsset> snapshot_;
		uint64_t revision_ = 0;
		uint64_t diskRevision_ = UINT64_MAX;
		bool dirty_ = false;
		bool historyPending_ = false;
	};

	//============================================================================
	//	AnimationClipDocumentStore class
	//	エディターの寿命内でアセットごとの編集元を保持する
	//============================================================================
	class AnimationClipDocumentStore {
	public:
		// 開いている編集元を返し、未読込なら保存内容から作る
		std::shared_ptr<AnimationClipDocument> Acquire(AssetDatabase& database, AssetID asset, std::string& error);
		// 既に開いている編集元だけを取得する
		std::shared_ptr<AnimationClipDocument> Find(AssetID asset) const;
		// 未保存の編集を実行用Worldへ公開する
		void Publish(ECSWorld& world);
		bool SaveDirty(AssetDatabase& database, AnimationClipManager* manager);
		void DiscardDirty();
		bool HasDirty() const;
		void SyncExternal(AssetDatabase& database);
		uint64_t NextEditSerial();
		uint64_t GetHistoryEpoch() const { return historyClock_->epoch; }
		uint64_t GetUndoSerial() const;
		uint64_t GetRedoSerial() const;
		bool RestoreHistory(bool redo);

	private:
		// アセットの参照元を一箇所へ集約する
		std::unordered_map<AssetID, std::shared_ptr<AnimationClipDocument>> documents_;
		std::shared_ptr<AnimationEditHistoryClock> historyClock_ = std::make_shared<AnimationEditHistoryClock>();
	};

	// 現在のエディターが所有する共有編集元を取得する
	std::shared_ptr<AnimationClipDocumentStore> GetAnimationClipDocumentStore(const EditorToolContext& context);
}
