#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Clips/AnimationClipAsset.h>

namespace Engine {

	enum class AnimationWrapMode : uint8_t { UseClip, Once, Loop, PingPong };
	enum class AnimationClipPhase : uint8_t { Play, Bridge, Interval };

	// 単一クリップの反復計算に使う設定
	struct AnimationPlaybackSettings {
		int32_t loopCount = 0;
		int32_t pingPongCount = 0;
		float interval = 0.0f;
		AnimationLoopBridgeSettings loopBridge{};
	};

	// 単一クリップの進行状態
	struct AnimationClipRuntime {
		float time = 0.0f;
		double normalizedTime = 0.0;
		int8_t dir = 1;
		int32_t repeatCount = 0;
		float delayRemaining = 0.0f;
		AnimationClipPhase phase = AnimationClipPhase::Play;
		float phaseTime = 0.0f;
		bool started = false;
		bool playing = false;
		bool finished = false;
		bool eventStartPending = true;
		bool sampled = false;
	};
}
