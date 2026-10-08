#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationClipEditTypes.h"
#include <Engine/Editor/Tools/Core/EditorToolContext.h>
#include <Engine/Core/Animation/Evaluation/AnimationClipEvaluator.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Editor/Animation/Curves/CurveEditorState.h>
#include <Engine/Editor/Animation/Curves/CurveGenerator.h>

// c++
#include <memory>

namespace Engine {

	class ECSWorldLifetime;

	//============================================================================
	//	AnimationClipEditSession class
	//	Clipの編集値とプレビューの復元状態を所有する
	//============================================================================
	class AnimationClipEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		AnimationClipEditSession() = default;
		~AnimationClipEditSession();
		AnimationClipEditSession(const AnimationClipEditSession&) = delete;
		AnimationClipEditSession& operator=(const AnimationClipEditSession&) = delete;

		// 選択したClipを読み込み、成功後に差し替える
		void LoadClipFromSelectedAsset(const EditorToolContext& context);
		// 編集値を保存して実行cacheを更新する
		void SaveClipToSelectedAsset(const EditorToolContext& context);
		// 編集値を破棄して保存済みClipを読み込む
		void RevertClipFromSelectedAsset(const EditorToolContext& context);
		// 現在値を既定値にしてPropertyを追加する
		void AddPropertyTrack(const AnimationPropertyDescriptor& desc, ECSWorld& world, const Entity& entity);
		// 現在のWorldから対象Entityを解決する
		Entity GetTargetEntity(const EditorToolContext& context) const;
		// 指定値と対象Entityから編集次元を決める
		AnimationClipEditDimension GetEffectiveEditDimension(const EditorToolContext& context) const;
		// 選択Trackの添字を有効範囲へ戻す
		void NormalizeSelectedTrackIndex();
		// 選択Trackの表示範囲を保持する
		void StoreSelectedTrackEditorView();
		// 選択Trackの表示範囲を取り込む
		void LoadSelectedTrackEditorView();
		// 指定した経過時間でプレビューを進める
		void UpdatePreviewPlayback(const EditorToolContext& context, float deltaTime);
		// 現在時刻の評価値を対象Entityへ適用する
		void ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive);
		// プレビューを終了して開始時の値へ戻す
		void EndPreviewAndRestore(const EditorToolContext& context);
		// 開始したWorldへプレビューの値を戻す
		void EndPreviewAndRestore();
		// 削除するPropertyだけ元の値へ戻す
		void RestoreAndDropPreviewBaseValue(const EditorToolContext& context, const AnimationPropertyBinding& binding);
		// 再生時間とプレビューを編集結果に合わせる
		void UpdateAutoDurationAndPreview(const EditorToolContext& context);

		// World切替とGizmoによる編集を検出する
		void UpdateExternalEdits(const EditorToolContext& context);
		// 旧対象を復元してプレビュー先を変える
		void SetPreviewTarget(const EditorToolContext& context, const UUID& nextTargetUUID);
		// 対象を復元してプレビュー先を解除する
		void ClearPreviewTarget(const EditorToolContext& context);
		// プレビューの再生と一時停止を切り替える
		void TogglePreviewPlayback(const EditorToolContext& context);
		// プレビューを復元して再生時刻を戻す
		void StopPreviewPlayback(const EditorToolContext& context);

		//--------- accessor -----------------------------------------------------

		AssetID& GetClipAssetID() { return clipAssetID_; }
		const AssetID& GetClipAssetID() const { return clipAssetID_; }
		AnimationClipAsset& GetClip() { return clip_; }
		const AnimationClipAsset& GetClip() const { return clip_; }
		bool GetHasClip() const { return hasClip_; }
		void MarkClipDirty() { clipDirty_ = true; }
		bool GetClipDirty() const { return clipDirty_; }
		const std::string& GetClipErrorText() const { return clipErrorText_; }
		const UUID& GetTargetEntityUUID() const { return targetEntityUUID_; }
		CurveEditorState& GetCurveState() { return curveState_; }
		const CurveEditorState& GetCurveState() const { return curveState_; }
		int& GetSelectedTrackIndex() { return selectedTrackIndex_; }
		int GetSelectedTrackIndex() const { return selectedTrackIndex_; }
		int GetEditorViewTrackIndex() const { return editorViewTrackIndex_; }
		bool GetPreviewActive() const { return previewActive_; }
		bool GetPreviewPlaying() const { return previewPlaying_; }
		float& GetPreviewTime() { return previewTime_; }
		float GetPreviewTime() const { return previewTime_; }
		float& GetPreviewSpeed() { return previewSpeed_; }
		float GetPreviewSpeed() const { return previewSpeed_; }
		AnimationClipEditDimension& GetEditDimension() { return editDimension_; }
		const AnimationClipEditDimension& GetEditDimension() const { return editDimension_; }
		CurveGeneratorState& GetGeneratorState() { return generatorState_; }
		const CurveGeneratorState& GetGeneratorState() const { return generatorState_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		// Entity参照を含めない編集対象Clip
		AssetID clipAssetID_{};
		AssetID loadedClipAssetID_{};
		AnimationClipAsset clip_{};
		bool hasClip_ = false;
		bool clipDirty_ = false;
		std::string clipErrorText_;

		// Clipに保存しないプレビュー対象
		UUID targetEntityUUID_{};

		// 複数Trackを同じCurveEditorで見るための状態
		CurveEditorState curveState_{};
		// 編集対象と表示範囲を保持するTrack
		int selectedTrackIndex_ = -1;
		int editorViewTrackIndex_ = -1;

		// プレビュー先の寿命と再生状態
		bool previewActive_ = false;
		ECSWorld* previewWorld_ = nullptr;
		std::weak_ptr<const ECSWorldLifetime> previewWorldLifetime_;
		Entity previewEntity_{};
		bool previewPlaying_ = false;
		float previewTime_ = 0.0f;
		float previewSpeed_ = 1.0f;
		std::vector<AnimationPreviewBaseValue> previewBaseValues_;
		// 外部編集の検出に使う最終適用値
		std::vector<AnimationPreviewBaseValue> lastAppliedValues_;
		// 前回のUndoとRedo件数
		size_t lastUndoCount_ = 0;
		size_t lastRedoCount_ = 0;

		// Transformの追加候補を絞る編集次元
		AnimationClipEditDimension editDimension_ = AnimationClipEditDimension::Auto;

		// カーブ生成の設定
		CurveGeneratorState generatorState_{};

		//--------- functions ----------------------------------------------------

		// 対象Entityの描画次元を調べる
		AnimationClipDetectedDimension DetectTargetDimension(const EditorToolContext& context) const;
		// Curve表示時刻を同期する
		void SyncCurveStateTime();
		// プレビュー開始時の値を保持する
		void BeginPreview(const EditorToolContext& context);
		// 未取得のPropertyの開始値を保持する
		void CachePreviewBaseValues(ECSWorld& world, const Entity& entity);
		// プレビュー開始時の値へ戻す
		void RestorePreviewBaseValues(ECSWorld& world, const Entity& entity);
		// 最後に適用した値を保持する
		void CaptureLastAppliedValues(ECSWorld& world, const Entity& entity);
		// 外部編集をプレビュー基準値へ反映する
		void SyncPreviewBaseFromEntityEdits(const EditorToolContext& context);
	};
}
