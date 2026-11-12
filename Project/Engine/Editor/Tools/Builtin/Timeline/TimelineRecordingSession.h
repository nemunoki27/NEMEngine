#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineAsset.h>
#include <Engine/Editor/Tools/Builtin/Animation/AnimationClipEditSession.h>
#include <Engine/Editor/UI/Common/AssetCreationDialog.h>
#include <Engine/Editor/Animation/AnimationKeyRecording.h>

// c++
#include <utility>
#include <unordered_map>

namespace Engine {

	struct TimelineRecordingState;

	//============================================================================
	//	TimelineRecordingSession class
	//	トラックのキー記録と共有クリップのカーブ編集を所有する
	//============================================================================
	class TimelineRecordingSession {
	public:
		TimelineRecordingSession() = default;
		~TimelineRecordingSession();
		// 対象トラックの記録を開始または終了する
		bool ToggleRecording(const EditorToolContext& context, TimelineAsset& asset, UUID trackID,
			UUID clipID, Entity target, double time);
		// 対象と時刻を確認して未保存の素材を公開する
		void Update(const EditorToolContext& context, TimelineAsset& asset, double time, bool playing);
		// 編集中の記録先を解除する
		void EndRecording();
		// 初回記録の保存先を共通画面で指定する
		void DrawCreateDialog(const EditorToolContext& context, TimelineAsset& asset);
		// 指定トラックの素材カーブを展開する
		bool DrawCurves(const EditorToolContext& context, TimelineAsset& asset, UUID trackID,
			UUID clipID, Entity target, double& time, ImVec2 size);
		// 選択中のキーを共通Inspectorで編集する
		bool DrawKeyInspector(const EditorToolContext& context);
		// 編集した素材をまとめて保存または破棄する
		bool Save(const EditorToolContext& context);
		void Discard();
		bool RestoreHistory(bool redo);

		//--------- accessor -----------------------------------------------------

		bool IsRecording(UUID trackID = {}) const;
		bool HasPendingEdits() const;
		UUID GetCurveTrackID() const { return curveTrackID_; }
		UUID GetCurveClipID() const { return curveClipID_; }
		bool HasSelectedKeys() const;
		const std::string& GetError() const;
		bool ConsumeAssetChanged() { return std::exchange(assetChanged_, false); }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- variables ----------------------------------------------------

		std::shared_ptr<TimelineRecordingState> recording_;
		std::vector<std::shared_ptr<AnimationClipDocument>> documents_;
		std::unordered_map<UUID, std::unique_ptr<AnimationClipEditSession>> curveSessions_;
		std::unordered_map<UUID, uint64_t> curveFitRevisions_;
		UUID curveTrackID_{}, curveClipID_{};
		AssetCreationDialog creationDialog_;
		UUID pendingTrack_{}, pendingClip_{}, pendingTarget_{};
		double pendingTime_ = 0.0;
		std::string error_;
		bool assetChanged_ = false;

		//--------- functions ----------------------------------------------------

		// 参照素材を共有編集元から取得する
		std::shared_ptr<AnimationClipDocument> OpenDocument(const EditorToolContext& context, const TimelineClip& clip);
		// 記録対象のWorldと素材を固定する
		bool BeginRecording(const EditorToolContext& context, const TimelineTrack& track,
			const TimelineClip& clip, Entity target, double time, double frameRate);
	};

}
