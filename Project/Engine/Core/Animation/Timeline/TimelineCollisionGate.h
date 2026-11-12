#pragma once

//============================================================================
//	include
//============================================================================
#include "TimelineControl.h"

namespace Engine {

	struct TimelinePlayerRecord;
	struct TimelinePlaybackState;
	struct TimelineCollisionGate {

		Entity target = Entity::Null();
		TimelineControlOwner control{};
	};

	// 判定区間の外では割り当て先のCollisionを無効にする
	void InitializeTimelineCollisionGates(ECSWorld& world, Entity owner,
		TimelinePlayerRecord& player, TimelinePlaybackState& state);
	void ApplyTimelineCollisionGates(ECSWorld& world, TimelinePlaybackState& state);
	void RefreshTimelineCollisionGates(ECSWorld& world, TimelinePlaybackState& state);
	// Clipの終了処理をすべて完了してから呼び出す
	void ReleaseTimelineCollisionGates(ECSWorld& world, TimelinePlaybackState& state, bool preview);
}
