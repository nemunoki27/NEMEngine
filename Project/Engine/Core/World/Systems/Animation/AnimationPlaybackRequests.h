#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/World/ECS/Systems/Context/SystemContext.h>

namespace Engine::AnimationPlaybackRequests {

	// 単独Clipを一時Groupとして再生要求へ渡す
	void PrepareDirectClip(AnimationPlayerComponent& player, SystemContext& context);
	// 評価前に使用中Clipの更新を公開する
	void RefreshActiveClips(const AnimationPlayerComponent& player, SystemContext& context);
	// 正規化した遷移時間を秒へ変換する
	float ResolveFadeDuration(const AnimationPlayerComponent& player, SystemContext& context);
	// 明示した再生時刻を適用し、このframeの時間進行を止める
	bool ApplyRequestedTime(AnimationPlayerComponent& player, SystemContext& context);
}
