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

namespace Engine {

	//============================================================================
	//	AnimationClipEditSession class
	//	Clipの編集値とプレビューの復元状態を所有する
	//============================================================================
	class AnimationClipEditSession {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		void LoadClipFromSelectedAsset(const EditorToolContext& context);
		void SaveClipToSelectedAsset(const EditorToolContext& context);
		void RevertClipFromSelectedAsset(const EditorToolContext& context);
		// Propertyを追加した時点ではキーを作らず、現在値をdefaultValueとして保持する
		void AddPropertyTrack(const AnimationPropertyDescriptor& desc, ECSWorld& world, const Entity& entity);
		Entity GetTargetEntity(const EditorToolContext& context) const;
		AnimationClipEditDimension GetEffectiveEditDimension(const EditorToolContext& context) const;
		void NormalizeSelectedTrackIndex();
		void StoreSelectedTrackEditorView();
		void LoadSelectedTrackEditorView();
		void UpdatePreviewPlayback(const EditorToolContext& context);
		// 現在時刻のClip評価値をTarget Entityへ直接反映する
		void ApplyPreviewAtCurrentTime(const EditorToolContext& context, bool keepActive);
		void EndPreviewAndRestore(const EditorToolContext& context);
		// Track削除時に、そのPropertyだけを元のシーン値へ戻しbaseからも取り除く
		void RestoreAndDropPreviewBaseValue(const EditorToolContext& context, const AnimationPropertyBinding& binding);
		void UpdateAutoDurationAndPreview(const EditorToolContext& context);

		void UpdateExternalEdits(const EditorToolContext& context);
		void SetPreviewTarget(const EditorToolContext& context, const UUID& nextTargetUUID);
		void ClearPreviewTarget(const EditorToolContext& context);
		void TogglePreviewPlayback(const EditorToolContext& context);
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

		// プレビュー適用先でHierarchyからD&Dで指定するがClip保存対象には含めない
		UUID targetEntityUUID_{};

		// 複数Trackを同じCurveEditorで見るための状態
		CurveEditorState curveState_{};
		// CurveEditorに表示しているTrackで複数PropertyのCurveが重ならないよう選択中Trackだけを表示する
		int selectedTrackIndex_ = -1;
		int editorViewTrackIndex_ = -1;

		// Preview再生状態でpreviewActive_はScrub中もtrueになりStop/Closeで必ず復元する
		bool previewActive_ = false;
		bool previewPlaying_ = false;
		float previewTime_ = 0.0f;
		float previewSpeed_ = 1.0f;
		std::vector<AnimationPreviewBaseValue> previewBaseValues_;
		// Previewでtoolが最後に書き込んだ値、Entityの現在値とズレていたら外部編集とみなす検知に使う
		std::vector<AnimationPreviewBaseValue> lastAppliedValues_;
		// 編集が起きたフレームだけ検知するため、前フレームのUndo/Redoカウントを覚えておく
		size_t lastUndoCount_ = 0;
		size_t lastRedoCount_ = 0;

		// Transform系Propertyの2D/3D候補を絞るための表示フィルタ
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
		// Propertyの開始値を保持済みか調べる
		bool HasPreviewBaseValue(const AnimationPropertyBinding& binding) const;
		// 未取得のPropertyの開始値を保持する
		void CachePreviewBaseValues(ECSWorld& world, const Entity& entity);
		// プレビュー開始時の値へ戻す
		void RestorePreviewBaseValues(ECSWorld& world, const Entity& entity);
		// 最後に適用した値を保持する
		void CaptureLastAppliedValues(ECSWorld& world, const Entity& entity);
		// 外部編集をプレビュー基準値へ反映する
		void SyncPreviewBaseFromEntityEdits(const EditorToolContext& context);
		// 指定ChannelへKeyを追加する
		void AddKeyToChannel(AnimationCurveTrack& track, uint32_t channelIndex, float time);
	};
}
