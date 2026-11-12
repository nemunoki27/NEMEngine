#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/UI/Common/AssetCreationDialog.h>
#include <Engine/Editor/Tools/Core/IEditorTool.h>
#include "TimelineEditorSession.h"
#include "TimelineEditOperations.h"
#include "TimelineRecordingSession.h"

// c++
#include <optional>
#include <unordered_set>

namespace Engine {

	//============================================================================
	//	TimelineTool class
	//	時間軸上のクリップと割り当て先を編集する
	//============================================================================
	class TimelineTool : public IEditorTool {
	public:
		void Tick(ToolContext& context) override;
		void OpenEditorTool() override;
		void DrawEditorTool(const EditorToolContext& context) override;
		void OpenAsset(AssetID assetID);
		void OpenPlayer(AssetID assetID, UUID owner);
		bool HasPendingEdits() const override;
		void RequestResolvePendingEdits() override;
		EditorToolCloseResult ConsumePendingEditCloseResult() override;
		void EndScenePreview() override;
		bool IsInspectionTargetValid(const EditorInspectionTarget& target) const override;
		bool DrawSelectionInspector(const EditorPanelContext& context, const EditorInspectionTarget& target) override;
		const ToolDescriptor& GetDescriptor() const override { return descriptor_; }

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		using ClipSelection = TimelineClipSelection;
		struct CopiedClip { UUID track{}, binding{}, parent{}; TimelineTrackKind kind{}; TimelineClip clip; };
		struct VisibleTrack { UUID id{}; int depth = 0; float y = 0.0f, height = 36.0f; };
		enum class PendingKind { Load, Create, Import, Close };
		struct PendingAction {

			PendingKind kind = PendingKind::Close;
			AssetID asset{};
			std::string name;
			bool assetOnly = false;
		};
		enum class DragMode { None, Move, Start, End };
		enum class ClipAction { None, Delete, Duplicate, Split };
		enum class TimeDisplay { Frames, Seconds, Timecode };

		ToolDescriptor descriptor_{
			.id = "engine.timeline", .name = "タイムライン", .category = "アニメーション",
			.owner = ToolOwner::Engine, .flags = ToolFlags::AllowPlayMode, .order = 3
		};
		TimelineEditorSession session_;
		TimelineRecordingSession recording_;
		bool visibleLastFrame_ = false;
		bool resolveClose_ = false;
		EditorToolCloseResult closeResult_ = EditorToolCloseResult::None;
		std::optional<PendingAction> pending_;
		std::optional<AssetID> queuedAsset_;
		bool queuedAssetOnly_ = true;
		UUID queuedOwner_{};
		AssetCreationDialog creationDialog_;
		UUID selectedTrack_{};
		EditorInspectionTarget inspectedTarget_{};
		std::vector<ClipSelection> selectedClips_;
		std::unordered_set<UUID> collapsedGroups_;
		std::vector<VisibleTrack> visibleTracks_;
		std::vector<CopiedClip> clipboard_;
		bool frames_ = true;
		bool snap_ = true;
		bool autoBlend_ = true;
		bool targetLocked_ = false;
		UUID lastFollowOwner_{};
		AssetID lastFollowAsset_{};
		uint64_t lastFollowAssetRevision_ = 0;
		std::weak_ptr<const ECSWorldLifetime> interactionWorld_;
		bool followPlayhead_ = true;
		bool showEvents_ = true;
		TimeDisplay timeDisplay_ = TimeDisplay::Frames;
		std::string timecodeEdit_;
		bool timecodeEditing_ = false;
		TimelineEditMode editMode_ = TimelineEditMode::Mix;
		std::unordered_set<UUID> expandedCurves_;
		double viewStart_ = 0.0;
		float pixelsPerSecond_ = 160.0f;
		float labelWidth_ = 300.0f;
		float rowHeight_ = 36.0f;
		float canvasWidth_ = 800.0f;
		ImVec2 canvasOrigin_{};
		double snapGuide_ = -1.0;
		int rangeDrag_ = 0;
		UUID pendingBindingTrack_{}, pendingBindingTarget_{};
		UUID movedTrack_{}, moveDestination_{};
		TimelineTrackDropPosition movePosition_ = TimelineTrackDropPosition::Before;
		UUID droppedEntity_{};
		double droppedTime_ = 0.0;
		DragMode dragMode_ = DragMode::None;
		ClipAction clipAction_ = ClipAction::None;
		TimelineAsset dragBefore_;
		UUID draggedClip_{};
		UUID draggedTrack_{};
		float dragMouseX_ = 0.0f;
		double dragViewStart_ = 0.0;
		double dragDelta_ = 0.0;
		bool dragApplied_ = false;
		bool boxSelecting_ = false;
		ImVec2 boxStart_{};
		ImVec2 boxCurrent_{};
		std::vector<std::pair<ClipSelection, ImVec4>> clipRects_;
		bool showBindings_ = true;
		bool showParameters_ = true;
		nlohmann::json customCatalog_ = nlohmann::json::array();
		double nextCatalogUpdate_ = 0.0;
		AssetID childAsset_{};
		uint64_t childRevision_ = 0;
		TimelineAsset childTimeline_;
		bool childLoaded_ = false;
		std::string childError_;
		std::string checkedClipData_;
		std::string clipDataError_;

		// 操作欄と未保存編集を描く
		void DrawToolbar(const EditorToolContext& context);
		// 選択対象と表示設定のメニューを描く
		void DrawTimelineSelector(const EditorToolContext& context, float width);
		void DrawFileMenu(const EditorToolContext& context);
		void DrawSettingsMenu();
		// 選択した対象へ追従しInspectorへ編集先を渡す
		void FollowSelection(const EditorToolContext& context);
		void SelectInspection(const EditorToolContext& context, UUID track = {}, UUID clip = {});
		// 対象の切り替え前に進行中の操作を確定して解除する
		void ResetInteraction();
		// 選択区間へ表示範囲を合わせる
		void FrameSelection(bool all);
		void DrawPending(const EditorToolContext& context);
		void RequestAction(const EditorToolContext& context, PendingAction action);
		bool ExecuteAction(const EditorToolContext& context, const PendingAction& action);
		// 時間軸とトラックの共通スクロール領域を描く
		void DrawCanvas(const EditorToolContext& context);
		void DrawEditToolbar(const EditorToolContext& context);
		void DrawZoomBar();
		void DrawRuler(const EditorToolContext& context, float height);
		// 選択した単位で目盛りの表示文字を作る
		std::string FormatTime(double time) const;
		double GetTickStep() const;
		void DrawCanvasInput(const EditorToolContext& context, ImVec2 origin, float width, float height);
		void DrawAssetDrop(const EditorToolContext& context, ImVec2 origin, float width, float height);
		void DrawTrackRow(const EditorToolContext& context, TimelineTrack& track, int depth,
			ImVec2 origin, float width, float timeX, float rowHeight);
		void DrawClip(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip,
			ImVec2 origin, float timeX, float laneWidth, float rowHeight);
		// 行内の対象設定と共有割り当ての変更を描く
		void DrawTrackBinding(const EditorToolContext& context, TimelineTrack& track, ImVec2 origin, float width);
		void SetTrackBinding(const EditorToolContext& context, UUID trackID, UUID target, bool shared);
		void DrawBindingChoice(const EditorToolContext& context);
		void UpdateDrag(const EditorToolContext& context);
		// Inspectorとキャンバスで同じ時間編集を使用する
		void ApplyClipTiming(UUID trackID, UUID clipID, double start, double duration, double clipIn);
		void RebuildVisibleTracks();
		// 選択トラックの記録とカーブ編集へ接続する
		void ToggleTrackRecording(const EditorToolContext& context, UUID trackID);
		bool IsTrackRecording(UUID trackID) const;
		bool DrawTrackCurves(const EditorToolContext& context, UUID trackID, ImVec2 size);
		// 参照する記録クリップを含めて保存と破棄を行う
		bool SaveAll(const EditorToolContext& context);
		void DiscardAll(const EditorToolContext& context);
		// 選択内容と割り当てを描く
		void DrawInspector(const EditorToolContext& context);
		void DrawBindings(const EditorToolContext& context);
		void DrawParameters();
		void DrawTrackInspector(TimelineTrack& track);
		void DrawConditions(TimelineTrack& track);
		void DrawClipInspector(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip);
		void DrawClipData(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip);
		// 型付きのNative設定を描画前に検証する
		bool ValidateClipData(const TimelineTrack& track, const TimelineClip& clip);
		// 独自クリップの登録情報から設定を描く
		void DrawCustomClipData(const EditorToolContext& context, TimelineClip& clip);
		// 既存の移動設定をTimelineの割り当てへ接続する
		void DrawMotionClipData(const EditorToolContext& context, TimelineClip& clip);
		// 共通プロパティ一覧と型別の値編集を描く
		void DrawPropertyClipData(const EditorToolContext& context, TimelineTrack& track, TimelineClip& clip);
		// トラックとクリップの構造を編集する
		void DrawAddTrackMenu(const EditorToolContext& context);
		void AddTrack(TimelineTrackKind kind, UUID parent = {});
		bool AddClip(TimelineTrack& track);
		void DeleteSelection();
		void DuplicateSelection();
		void CopySelection();
		void PasteSelection();
		void RemoveTrack(UUID id);
		void DuplicateTrack(UUID id);
		void NormalizeSelection();
		// 永続識別子から編集対象を引く
		TimelineTrack* FindTrack(UUID id);
		TimelineClip* FindClip(UUID track, UUID clip);
		bool IsSelected(UUID track, UUID clip) const;
		Entity ResolveTrackTarget(const EditorToolContext& context, const TimelineTrack& track) const;
	};
}
