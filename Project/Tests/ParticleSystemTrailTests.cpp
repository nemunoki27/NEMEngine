#include "ParticleSystemRuntimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Systems/Effect/ParticleTrailUpdater.h>

using namespace Engine;

bool NEMTests::TestParticleSystemTrails() {

	ParticleTrailSettings settings;
	settings.enabled = true;
	settings.maxTrails = 2;
	settings.maxPoints = 3;
	settings.pointLifetime = 0.3f;
	settings.keepAfterParticleDeath = true;
	ParticleTrailStorage pool;
	pool.Configure(settings);
	const auto first = pool.Allocate();
	const auto second = pool.Allocate();
	if (pool.Allocate().IsValid() || pool.GetOverflowCount() != 1) return false;
	auto& trail = *pool.TryGet(first);
	for (int i = 0; i < 5; ++i) pool.Record(trail, {Vector3(float(i), 0, 0)}, 0.1f);
	if (trail.count != 3 || pool.GetPoint(trail, 0).position.x != 2) return false;
	pool.Record(trail, {Vector3(4.01f, 0, 0)}, 0.1f);
	if (trail.count != 3 || trail.head.position.x != 4.01f) return false;
	pool.Release(first);
	const auto replacement = pool.Allocate();
	if (pool.TryGet(first) || replacement == first || !pool.TryGet(second)) return false;
	pool.clear();
	if (pool.TryGet(replacement) || !pool.empty() || pool.GetOverflowCount() != 0) return false;

	// 粒子スロットを再利用しても、死亡済みの尾を別世代で維持する
	ParticleEmitterRuntime emitter;
	ParticleTrailUpdater updater;
	Particle particle;
	particle.id = 42;
	particle.worldPos = Vector3(1, 0, 0);
	particle.color = Color4(0.2f, 0.3f, 0.4f, 0.5f);
	emitter.particles.Append(particle);
	updater.RecordTrails(emitter, settings, 0.1f);
	particle = emitter.particles.GetMutable(0);
	const ParticleTrailHandle old{particle.trailIndex, particle.trailGeneration};
	particle.worldPos.x = 2;
	emitter.particles.Set(0, particle);
	updater.RecordTrails(emitter, settings, 0.1f);
	emitter.particles.clear();
	updater.RecordTrails(emitter, settings, 0.05f);
	if (!emitter.trails.TryGet(old) || !emitter.trails.TryGet(old)->detached) return false;
	particle = Particle{};
	particle.id = 43;
	emitter.particles.Append(particle);
	updater.RecordTrails(emitter, settings, 0.01f);
	if (emitter.trails.GetHandles().size() != 2 || !emitter.trails.TryGet(old)) return false;
	emitter.particles.clear();
	updater.RecordTrails(emitter, settings, 0.5f);
	if (!emitter.trails.empty()) return false;

	// 配列順に依存せず、新しい粒子を発生順で結ぶ
	settings.mode = ParticleTrailMode::Ribbon;
	for (uint64_t id : {9ull, 2ull, 12ull, 5ull}) {
		Particle value;
		value.id = id;
		value.worldPos.x = float(id);
		emitter.particles.Append(value);
	}
	updater.RecordTrails(emitter, settings, 0.01f);
	const auto* ribbon = emitter.trails.TryGet(emitter.ribbon);
	if (!ribbon || emitter.trails.GetCapacity() != 1 || ribbon->count != 3) return false;
	if (emitter.trails.GetPoint(*ribbon, 0).position.x != 5 || emitter.trails.GetPoint(*ribbon, 1).position.x != 9 ||
		emitter.trails.GetPoint(*ribbon, 2).position.x != 12) return false;
	emitter.particles.RemoveSwap(1);
	updater.RecordTrails(emitter, settings, 0.01f);
	return emitter.trails.GetPoint(*emitter.trails.TryGet(emitter.ribbon), 0).position.x == 5;
}
