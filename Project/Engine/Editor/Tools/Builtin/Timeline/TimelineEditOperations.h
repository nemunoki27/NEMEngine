#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineAsset.h>

// c++
#include <span>

namespace Engine {

	enum class TimelineEditMode : uint8_t { Mix, Ripple, Replace };
	enum class TimelineClipEditKind : uint8_t { Move, Start, End };
	enum class TimelineTrackDropPosition : uint8_t { Before, After, IntoGroup };
	struct TimelineClipSelection {

		UUID track{}, clip{};
	};

	namespace TimelineEditOperations {

		// 素材時刻を保って分割できる種類か調べる
		bool CanSplit(TimelineTrackKind kind, const TimelineClip& clip);
		// 重なりをブレンドとして評価できる種類か調べる
		bool SupportsBlend(TimelineTrackKind kind);
		// 操作開始時の値から選択クリップと周囲を編集する
		bool Apply(TimelineAsset& asset, const TimelineAsset& before, std::span<const TimelineClipSelection> selection,
			UUID anchorTrack, UUID anchorClip, TimelineClipEditKind kind, double delta, TimelineEditMode mode, bool autoBlend);
		// 指定時刻で素材の連続性を保って分割する
		bool Split(TimelineAsset& asset, UUID trackID, UUID clipID, double time);
		// 貼り付けと別トラックへの移動へ編集モードを適用する
		bool Insert(TimelineAsset& asset, UUID trackID, TimelineClip clip, TimelineEditMode mode, bool autoBlend);
		// クリップ追従時の長さを更新する
		void UpdateDuration(TimelineAsset& asset);
		// 選択外の端点と再生位置へ吸着する
		double SnapTime(const TimelineAsset& asset, std::span<const TimelineClipSelection> selection,
			double time, double playhead, double tolerance);
		// 循環を作らずトラックを並べ替える
		bool MoveTrack(TimelineAsset& asset, UUID trackID, UUID destinationID, TimelineTrackDropPosition position);
	}
}
