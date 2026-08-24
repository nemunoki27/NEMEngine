#include "ParticleSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

// c++
#include <algorithm>

//============================================================================
//	ParticleSystem trail methods
//============================================================================
void Engine::ParticleSystem::RecordTrails(ParticleGroupRuntimeState& state,
	const ParticleTrailSettings& trail, float deltaTime) {

	aliveTrailIDs_.clear();
	aliveTrailIDs_.reserve(state.particles.size());
	for (const Particle& particle : state.particles) {
		aliveTrailIDs_.insert(particle.id);
	}

	const bool keepAfterParticleDeath = trail.keepAfterParticleDeath && 0.0f < trail.pointLifetime;
	for (auto it = state.trails.begin(); it != state.trails.end();) {

		ParticleTrailRuntime& runtime = it->second;
		const bool alive = aliveTrailIDs_.contains(it->first);
		if (!alive && !keepAfterParticleDeath) {
			it = state.trails.erase(it);
			continue;
		}
		runtime.detached = !alive;
		if (alive) {
			runtime.hasOwner = false;
			runtime.detachedThisFrame = false;
		}
		for (ParticleTrailPoint& point : runtime.points) {
			point.age += deltaTime;
		}
		if (runtime.detached) {
			runtime.head.age += deltaTime;
		}
		if (0.0f < trail.pointLifetime) {
			while (!runtime.points.empty() && trail.pointLifetime < runtime.points.front().age) {
				runtime.points.pop_front();
			}
		}
		if (runtime.detached && runtime.points.empty()) {
			it = state.trails.erase(it);
			continue;
		}
		++it;
	}

	const int32_t maxPoints = (std::max)(trail.maxPoints, 2);
	for (const Particle& particle : state.particles) {

		const Vector3 worldPos = particle.worldPos;
		ParticleTrailRuntime& runtime = state.trails[particle.id];
		runtime.head = ParticleTrailPoint{ worldPos, 0.0f, particle.phaseIndex };
		runtime.detached = false;
		runtime.hasOwner = false;
		runtime.detachedThisFrame = false;
		std::deque<ParticleTrailPoint>& points = runtime.points;

		if (points.empty()) {
			points.emplace_back(runtime.head);
			continue;
		}
		const Vector3 diff = worldPos - points.back().position;
		if (trail.minDistance * trail.minDistance <= Vector3::Dot(diff, diff)) {

			points.emplace_back(runtime.head);
			if (maxPoints < static_cast<int32_t>(points.size())) {
				points.pop_front();
			}
		}
	}
}

void Engine::ParticleSystem::UpdateDetachedTrailOwners(ParticleGroupRuntimeState& state,
	const GroupRuntime& group, const ParticlePhaseParentSettings& parentSettings,
	bool useAssetParentSettings, const std::vector<ParentRuntime>& parents,
	const ParticleTrailSettings& trail, float deltaTime) const {

	if (!trail.keepAfterParticleDeath) {
		return;
	}
	for (auto& [id, runtime] : state.trails) {

		if (!runtime.detached || !runtime.hasOwner) {
			continue;
		}
		Particle& owner = runtime.owner;
		if (trail.continueUpdateAfterParticleDeath) {

			if (!runtime.detachedThisFrame) {
				owner.previousAge = owner.age;
				owner.previousPhaseIndex = owner.phaseIndex;
				owner.age += deltaTime;
			}
			if (owner.phaseIndex < group.phases.size()) {

				const PhaseRuntime& phase = group.phases[owner.phaseIndex];
				UpdateParticleParent(owner, ResolveParticleParentSettings(
					phase, parentSettings, useAssetParentSettings), parents[owner.phaseIndex]);
			}
			owner.pos += owner.velocity * deltaTime;
			if (owner.phaseIndex < group.phases.size()) {

				const PhaseRuntime& phase = group.phases[owner.phaseIndex];
				if (phase.hasUpdateBatch) {
					ExecuteUpdateModules(std::span<Particle>(&owner, 1), phase, deltaTime);
				} else {
					ApplyUpdateModules(owner, phase, deltaTime);
				}
			}
		}
		const ParentRuntime* parent = owner.phaseIndex < parents.size() ? &parents[owner.phaseIndex] : nullptr;
		RefreshParticleWorldTransform(owner, parent);
		runtime.head.position = owner.worldPos;
		runtime.head.phaseIndex = owner.phaseIndex;
		runtime.detachedThisFrame = false;
	}
}
