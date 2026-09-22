#include "AnimationPlaybackTime.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationPlayerComponent.h>
#include <Engine/Core/Animation/Clips/AnimationClipManager.h>
#include <Engine/Core/Animation/Properties/AnimationPropertyRegistry.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::AnimationPlaybackTime {

	Engine::AnimationWrapMode EffectiveWrap(Engine::AnimationWrapMode wrap, bool clipLoop) {

		if (wrap != Engine::AnimationWrapMode::UseClip) {
			return wrap;
		}
		return clipLoop ? Engine::AnimationWrapMode::Loop : Engine::AnimationWrapMode::Once;
	}

	float AdvanceTime(float duration, Engine::AnimationWrapMode wrap,
		float time, int8_t& dir, float delta, bool& finished) {

		finished = false;
		const float dur = (std::max)(duration, 0.001f);
		switch (wrap) {
		case Engine::AnimationWrapMode::Once: {

			float t = time + delta;
			if (t >= dur) { t = dur; finished = true; } else if (t < 0.0f) { t = 0.0f; finished = true; }
			return t;
		}
		case Engine::AnimationWrapMode::PingPong: {

			// 端を越えた分を反射して往復させる
			float t = time + delta * static_cast<float>(dir);
			if (t > dur) { t = dur - (t - dur); dir = -1; } else if (t < 0.0f) { t = -t; dir = 1; }
			return (std::clamp)(t, 0.0f, dur);
		}
		case Engine::AnimationWrapMode::Loop:
		default: {

			float t = std::fmod(time + delta, dur);
			if (t < 0.0f) { t += dur; }
			return t;
		}
		}
	}

	bool CompleteLoopIteration(Engine::AnimationClipRuntime& rt, const Engine::AnimationState& state, float dur) {

		++rt.repeatCount;
		rt.phase = Engine::AnimationClipPhase::Play;
		rt.phaseTime = 0.0f;
		if (state.loopCount > 0 && rt.repeatCount >= state.loopCount) {
			rt.time = dur;
			rt.playing = false;
			rt.finished = true;
			return false;
		}
		rt.time = 0.0f;
		return true;
	}

	void AdvanceLoop(Engine::AnimationClipRuntime& rt, const Engine::AnimationState& state, float dur, float delta) {

		const bool bridgeOn = state.loopBridge.enabled && state.loopBridge.duration > 0.0f;
		const float bridgeDur = bridgeOn ? (std::max)(state.loopBridge.duration, 0.001f) : 0.0f;
		const float interval = (std::max)(state.interval, 0.0f);

		float remaining = delta;
		for (int guard = 0; remaining > 0.0f && guard < 16; ++guard) {

			if (rt.phase == Engine::AnimationClipPhase::Play) {

				const float room = dur - rt.time;
				if (remaining < room) { rt.time += remaining; return; }
				remaining -= room;
				rt.time = dur;
				if (bridgeOn) { rt.phase = Engine::AnimationClipPhase::Bridge; rt.phaseTime = 0.0f; } else if (interval > 0.0f) { rt.phase = Engine::AnimationClipPhase::Interval; rt.phaseTime = 0.0f; } else if (!CompleteLoopIteration(rt, state, dur)) { return; }
			} else if (rt.phase == Engine::AnimationClipPhase::Bridge) {

				const float room = bridgeDur - rt.phaseTime;
				if (remaining < room) { rt.phaseTime += remaining; return; }
				remaining -= room;
				// 繋ぎ補間は開始ポーズ(t=0)へ収束するので、以降はそこを保持する
				rt.time = 0.0f;
				if (interval > 0.0f) { rt.phase = Engine::AnimationClipPhase::Interval; rt.phaseTime = 0.0f; } else if (!CompleteLoopIteration(rt, state, dur)) { return; }
			} else {

				const float room = interval - rt.phaseTime;
				if (remaining < room) { rt.phaseTime += remaining; return; }
				remaining -= room;
				if (!CompleteLoopIteration(rt, state, dur)) { return; }
			}
		}
	}

	void AdvancePingPong(Engine::AnimationClipRuntime& rt, const Engine::AnimationState& state, float dur, float delta) {

		const float interval = (std::max)(state.interval, 0.0f);

		// インターバル待機中は消化してから逆再生を再開する
		if (rt.phase == Engine::AnimationClipPhase::Interval) {

			rt.phaseTime += delta;
			if (rt.phaseTime < interval) { return; }
			delta = rt.phaseTime - interval;
			rt.phase = Engine::AnimationClipPhase::Play;
			rt.phaseTime = 0.0f;
			rt.dir = 1;
			rt.time = 0.0f;
			if (delta <= 0.0f) { return; }
		}

		bool finished = false;
		const int8_t prevDir = rt.dir;
		rt.time = AdvanceTime(dur, Engine::AnimationWrapMode::PingPong, rt.time, rt.dir, delta, finished);

		// 開始位置(0)へ戻った(dir -1→+1)= 1往復完了
		if (prevDir == -1 && rt.dir == 1) {

			++rt.repeatCount;
			if (state.pingPongCount > 0 && rt.repeatCount >= state.pingPongCount) {
				rt.time = 0.0f;
				rt.playing = false;
				rt.finished = true;
				return;
			}
			if (interval > 0.0f) {
				rt.phase = Engine::AnimationClipPhase::Interval;
				rt.phaseTime = 0.0f;
				rt.time = 0.0f;
			}
		}
	}
}
