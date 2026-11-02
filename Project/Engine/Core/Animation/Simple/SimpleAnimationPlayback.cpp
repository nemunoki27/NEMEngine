#include "SimpleAnimationPlayback.h"

// c++
//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>

void Engine::StartSimpleAnimation(SimpleAnimationPlayback& playback, const SimpleAnimationSettings& settings,
	const SimpleAnimationValue& initial) {

	playback = {};
	playback.initial = initial.index() == settings.start.index() ? initial : settings.start;
	playback.type = settings.start.index();
	playback.mode = settings.mode;
	playback.playing = true;
	playback.initialized = true;
	SampleSimpleAnimation(playback, settings);
}

void Engine::SampleSimpleAnimation(SimpleAnimationPlayback& playback, const SimpleAnimationSettings& settings) {

	auto evaluation = settings;
	if (settings.startFromCurrent && settings.mode == SimpleAnimationMode::Interpolate) {
		evaluation.start = playback.initial;
		if (const auto* rotation = std::get_if<Quaternion>(&playback.initial)) {
			evaluation.startEuler = Quaternion::ToEulerAngles(*rotation);
		}
	}
	playback.sample = EvaluateSimpleAnimation(evaluation, playback.time);
	if (playback.sample.valid && settings.mode != SimpleAnimationMode::Interpolate && settings.blendDuration > 0.0) {
		const double elapsed = (std::max)(playback.time - settings.delay, 0.0);
		const float progress = static_cast<float>((std::min)(elapsed / settings.blendDuration, 1.0));
		playback.sample.value = BlendSimpleAnimation(playback.initial, playback.sample.value, EasedValue(settings.easing, progress));
		if (elapsed < settings.blendDuration) playback.sample.finished = false;
	}
	if (playback.sample.finished) playback.playing = false;
}

void Engine::AdvanceSimpleAnimation(SimpleAnimationPlayback& playback, const SimpleAnimationSettings& settings, double deltaTime) {

	if (!playback.initialized) return;
	if (playback.type != settings.start.index() || playback.mode != settings.mode) {
		StartSimpleAnimation(playback, settings, settings.start);
	}
	if (playback.playing && !playback.paused && std::isfinite(deltaTime) && deltaTime > 0 &&
		std::isfinite(settings.speed) && settings.speed >= 0) {
		playback.time += deltaTime * settings.speed;
	}
	SampleSimpleAnimation(playback, settings);
}
