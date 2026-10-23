#include "ParticleTrailUpdater.h"

//============================================================================
// include
//============================================================================
#include <algorithm>

using namespace Engine;

namespace {

	ParticleTrailPoint MakePoint(const Particle& particle) {

		return {particle.worldPos, 0.0f, particle.phaseIndex, particle.color, particle.emissive, particle.alphaReference};
	}
}

void ParticleTrailUpdater::RecordTrails(ParticleEmitterRuntime& state,
	const ParticleTrailSettings& settings, float deltaTime) {

	state.trails.Configure(settings);
	++state.trailSerial;
	expired_.clear();
	for (const auto handle : state.trails.GetHandles()) {
		auto& trail = *state.trails.TryGet(handle);
		state.trails.Expire(trail, deltaTime, settings.pointLifetime);
	}
	if (settings.mode == ParticleTrailMode::Ribbon) {
		RecordRibbon(state);
	} else {
		for (size_t i = 0; i < state.particles.size(); ++i) {
			auto particle = state.particles.GetMutable(i);
			if (particle.trailIndex == UINT32_MAX && particle.trailGeneration == UINT32_MAX) continue;
			ParticleTrailHandle handle{particle.trailIndex, particle.trailGeneration};
			auto* trail = state.trails.TryGet(handle);
			if (!trail) {
				handle = state.trails.Allocate();
				trail = state.trails.TryGet(handle);
				particle.trailIndex = handle.index;
				particle.trailGeneration = handle.IsValid() ? handle.generation : UINT32_MAX;
				state.particles.Set(i, particle);
			}
			if (!trail) continue;
			trail->detached = false;
			trail->lastTouched = state.trailSerial;
			state.trails.Record(*trail, MakePoint(particle), settings.minDistance);
		}
	}
	// 死亡した粒子を持たず、最後の点列だけを残す
	for (const auto handle : state.trails.GetHandles()) {
		const auto* trail = state.trails.TryGet(handle);
		if (trail->lastTouched == state.trailSerial) continue;
		if (!settings.keepAfterParticleDeath || trail->count == 0) {
			expired_.push_back(handle);
		} else {
			state.trails.TryGet(handle)->detached = true;
		}
	}
	for (const auto handle : expired_) state.trails.Release(handle);
}

void ParticleTrailUpdater::RecordRibbon(ParticleEmitterRuntime& state) {

	if (state.particles.empty()) return;
	auto* trail = state.trails.TryGet(state.ribbon);
	if (!trail) {
		state.ribbon = state.trails.Allocate();
		trail = state.trails.TryGet(state.ribbon);
	}
	if (!trail) return;
	ribbonParticles_.clear();
	for (const auto particle : state.particles) ribbonParticles_.push_back(particle);
	// 記録上限の新しい粒子を、常に同じ発生順で結ぶ
	const size_t count = (std::min)(ribbonParticles_.size(), size_t(state.trails.GetPointCapacity()));
	std::partial_sort(ribbonParticles_.begin(), ribbonParticles_.begin() + count, ribbonParticles_.end(),
		[](const Particle& a, const Particle& b) { return a.id > b.id; });
	trail->begin = 0;
	trail->count = 0;
	trail->detached = false;
	trail->lastTouched = state.trailSerial;
	for (size_t i = count; i > 0; --i) state.trails.Record(*trail, MakePoint(ribbonParticles_[i - 1]), 0.0f);
}
