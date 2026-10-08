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
#include <limits>

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
			if (t >= dur) { t = dur; finished = true; } else if (t <= 0.0f && delta < 0.0f) { t = 0.0f; finished = true; }
			return t;
		}
		case Engine::AnimationWrapMode::PingPong: {

			// 周期へ畳み込み、複数回の折返しにも対応する
			const double period = static_cast<double>(dur) * 2.0;
			double phase = std::fmod((dir > 0 ? time : period - time) + delta, period);
			if (phase < 0.0) phase += period;
			dir = phase < dur ? 1 : -1;
			return static_cast<float>(phase <= dur ? phase : period - phase);
		}
		case Engine::AnimationWrapMode::Loop:
		default: {

			float t = std::fmod(time + delta, dur);
			if (t < 0.0f) { t += dur; }
			return t;
		}
		}
	}

	void RecordInterval(AnimationClipRuntime& rt, float from, float to, std::vector<Interval>* intervals) {

		if (intervals && (from != to || rt.eventStartPending)) {
			intervals->push_back({ from, to, rt.eventStartPending });
		}
		rt.eventStartPending = false;
	}

	bool CompleteLoopIteration(AnimationClipRuntime& rt, const AnimationState& state, float dur) {

		if (rt.repeatCount < (std::numeric_limits<int32_t>::max)()) ++rt.repeatCount;
		rt.phase = AnimationClipPhase::Play;
		rt.phaseTime = 0.0f;
		if (state.loopCount > 0 && rt.repeatCount >= state.loopCount) {

			rt.time = rt.dir > 0 ? dur : 0.0f;
			rt.playing = false;
			rt.finished = true;
			return false;
		}
		rt.time = rt.dir > 0 ? 0.0f : dur;
		rt.eventStartPending = true;
		return true;
	}

	void AdvanceLoop(AnimationClipRuntime& rt, const AnimationState& state, float dur, float delta,
		std::vector<Interval>* intervals) {

		if (!std::isfinite(delta) || !std::isfinite(dur) || dur <= 0.0f || delta == 0.0f) return;
		rt.dir = delta > 0.0f ? 1 : -1;
		const float bridgeDuration = state.loopBridge.enabled ? (std::max)(state.loopBridge.duration, 0.0f) : 0.0f;
		const float intervalDuration = (std::max)(state.interval, 0.0f);
		double remaining = std::abs(static_cast<double>(delta));
		// 本編、繋ぎ補間、待機を残り時間がなくなるまで順に進める
		while (remaining > 0.0 && !rt.finished) {

			if (rt.phase == AnimationClipPhase::Play) {

				// Event不要なら完了する周回をまとめて進める
				if (!intervals && rt.time == (rt.dir > 0 ? 0.0f : dur)) {

					const double cycleDuration = static_cast<double>(dur) + bridgeDuration + intervalDuration;
					double cycles = std::floor(remaining / cycleDuration);
					if (state.loopCount > 0) cycles = (std::min)(cycles, static_cast<double>((std::max)(state.loopCount - rt.repeatCount - 1, 0)));
					if (cycles > 0.0) {

						remaining -= cycles * cycleDuration;
						rt.normalizedTime += cycles;
						rt.repeatCount = static_cast<int32_t>((std::min)(static_cast<double>(rt.repeatCount) + cycles,
							static_cast<double>((std::numeric_limits<int32_t>::max)())));
						if (remaining <= 0.0) break;
					}
				}

				const float from = rt.time;
				const double room = rt.dir > 0 ? dur - rt.time : rt.time;
				const double consumed = (std::min)(remaining, (std::max)(room, 0.0));
				rt.time += static_cast<float>(consumed) * rt.dir;
				rt.normalizedTime += consumed / dur;
				RecordInterval(rt, from, rt.time, intervals);
				remaining -= consumed;
				if (consumed < room) break;
				rt.time = rt.dir > 0 ? dur : 0.0f;
				// 最後の周回は繋ぎ補間を挟まず終端で止める
				if (state.loopCount > 0 && rt.repeatCount >= state.loopCount - 1) {

					CompleteLoopIteration(rt, state, dur);
					break;
				}
				if (bridgeDuration > 0.0f) {

					rt.phase = AnimationClipPhase::Bridge;
					rt.phaseTime = 0.0f;
				} else if (intervalDuration > 0.0f) {

					rt.phase = AnimationClipPhase::Interval;
					rt.phaseTime = 0.0f;
				} else if (!CompleteLoopIteration(rt, state, dur)) break;
			} else {

				const float duration = rt.phase == AnimationClipPhase::Bridge ? bridgeDuration : intervalDuration;
				const double room = (std::max)(static_cast<double>(duration - rt.phaseTime), 0.0);
				const double consumed = (std::min)(remaining, room);
				rt.phaseTime += static_cast<float>(consumed);
				remaining -= consumed;
				if (consumed < room) break;
				if (rt.phase == AnimationClipPhase::Bridge && intervalDuration > 0.0f) {

					rt.time = rt.dir > 0 ? 0.0f : dur;
					rt.phase = AnimationClipPhase::Interval;
					rt.phaseTime = 0.0f;
				} else if (!CompleteLoopIteration(rt, state, dur)) break;
			}
		}
	}

	void AdvancePingPong(AnimationClipRuntime& rt, const AnimationState& state, float dur, float delta,
		std::vector<Interval>* intervals) {

		if (!std::isfinite(delta) || !std::isfinite(dur) || dur <= 0.0f || delta == 0.0f) return;
		const int8_t sign = delta > 0.0f ? 1 : -1;
		const float intervalDuration = (std::max)(state.interval, 0.0f);
		double remaining = std::abs(static_cast<double>(delta));
		// 一度の更新で跨いだ折返しをすべて処理する
		while (remaining > 0.0 && !rt.finished) {

			if (rt.phase == AnimationClipPhase::Interval) {

				const double room = (std::max)(static_cast<double>(intervalDuration - rt.phaseTime), 0.0);
				const double consumed = (std::min)(remaining, room);
				rt.phaseTime += static_cast<float>(consumed);
				remaining -= consumed;
				if (consumed < room) break;
				rt.phase = AnimationClipPhase::Play;
				rt.phaseTime = 0.0f;
				continue;
			}
			// 往復の開始点にいる場合だけ周回をまとめる
			if (!intervals && rt.dir == 1 && rt.time == (sign > 0 ? 0.0f : dur)) {

				const double cycleDuration = static_cast<double>(dur) * 2.0 + intervalDuration;
				double cycles = std::floor(remaining / cycleDuration);
				if (state.pingPongCount > 0) cycles = (std::min)(cycles, static_cast<double>((std::max)(state.pingPongCount - rt.repeatCount - 1, 0)));
				if (cycles > 0.0) {

					remaining -= cycles * cycleDuration;
					rt.normalizedTime += cycles * 2.0;
					rt.repeatCount = static_cast<int32_t>((std::min)(static_cast<double>(rt.repeatCount) + cycles,
						static_cast<double>((std::numeric_limits<int32_t>::max)())));
					if (remaining <= 0.0) break;
				}
			}
			const int8_t direction = sign * rt.dir;
			const float from = rt.time;
			const double room = direction > 0 ? dur - rt.time : rt.time;
			const double consumed = (std::min)(remaining, (std::max)(room, 0.0));
			rt.time += static_cast<float>(consumed) * direction;
			rt.normalizedTime += consumed / dur;
			RecordInterval(rt, from, rt.time, intervals);
			remaining -= consumed;
			if (consumed < room) break;
			rt.time = direction > 0 ? dur : 0.0f;
			rt.dir = -rt.dir;
			if (direction == -sign) {

				if (rt.repeatCount < (std::numeric_limits<int32_t>::max)()) ++rt.repeatCount;
				if (state.pingPongCount > 0 && rt.repeatCount >= state.pingPongCount) {

					rt.playing = false;
					rt.finished = true;
					break;
				}
				if (intervalDuration > 0.0f) {

					rt.phase = AnimationClipPhase::Interval;
					rt.phaseTime = 0.0f;
				}
			}
		}
	}
}
