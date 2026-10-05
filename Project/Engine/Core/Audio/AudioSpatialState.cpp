#include "AudioSpatialState.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>
#include <cmath>

float Engine::CalculateAudioDistanceGain(const AudioSpatialState& source, const AudioListenerState& listener) {

	if (!listener.active) { return 0.0f; }
	float distance = (source.position - listener.position).Length();
	if (!std::isfinite(distance)) { return 0.0f; }
	float minimum = std::isfinite(source.minDistance) ? std::max(source.minDistance, 0.0001f) : 1.0f;
	if (distance <= minimum) { return 1.0f; }
	// Logarithmicは最大距離を超えても減衰を続ける
	if (source.rolloffMode == AudioRolloffMode::Logarithmic) { return minimum / distance; }
	float maximum = std::isfinite(source.maxDistance) ? std::max(source.maxDistance, minimum) : 500.0f;
	return maximum > minimum ? std::clamp((maximum - distance) / (maximum - minimum), 0.0f, 1.0f) : 0.0f;
}
