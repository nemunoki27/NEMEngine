#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Timeline/TimelineClipInstance.h>
#include <Engine/Core/World/Components/Animation/TimelinePlayerComponent.h>

namespace Engine {

	// PlayerとEditorプレビューで同じ時刻評価を使用する
	bool EvaluateTimelineAt(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		const TimelineAsset& asset, double time, SystemContext& context,
		TimelineEvaluationMode mode = TimelineEvaluationMode::Seek, bool emitEvents = false, bool isScrubbing = false);
	// 区間通過を含めて再生時間を進める
	void AdvanceTimelinePlayer(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		const TimelineAsset& asset, double deltaTime, SystemContext& context);
	// プレビューと実行の復元方針を維持して制御を解放する
	void ReleaseTimelinePlayer(ECSWorld& world, Entity owner, TimelinePlayerRecord& player,
		SystemContext& context, TimelineExitReason reason = TimelineExitReason::Stopped);
}
