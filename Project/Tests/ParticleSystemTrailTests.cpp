#include "ParticleSystemRuntimeTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleTrailDataBuilder.h>
#include <Engine/Core/World/Systems/Effect/ParticleTrailUpdater.h>

using namespace Engine;

namespace {

	bool CheckBeamGeometry() {

		BeamRendererRecord beam;
		beam.settings.width = 4.0f;
		beam.settings.widthMultiplier = 2.0f;
		beam.settings.is2D = true;
		beam.settings.useWidthCurve = true;
		const auto uvMatrix = Matrix4x4::MakeTranslateMatrix({0.25f, 0.5f, 0});
		beam.settings.widthCurve.channel.AddKey(0, 1);
		beam.settings.widthCurve.channel.AddKey(1, 0);
		beam.runtime.time = 0.5;
		beam.runtime.points = {
			{{0, 0, 0}, 0, 0, 1, true}, {{4, 0, 0}, 1, 4, 1},
			{{2, 0, 0}, 0.5f, 2, 0.5f, true}, {{2, 1, 0}, 0.75f, 3, 0}
		};
		ParticleCustomParameterLayout layout{};
		layout.stride = 1;
		layout.defaultData = {8};
		ParticleTrailRenderData data;
		ParticleTrailDataBuilder::Build(beam, beam.settings.blendMode, uvMatrix, layout, data);
		if (data.points.size() != 4 || data.segments != std::vector<uint32_t>{0, 2} ||
			data.materials.size() != 2 || data.customParameters != std::vector<uint8_t>{8, 8}) return false;
		if (data.points[0].side.y != -1 || data.points[2].side.x != 1 ||
			data.points[0].halfWidth != 4 || data.points[2].halfWidth != 1 ||
			data.points[2].color != Color4::White() || data.points[3].ribbonT != 0.75f || !(data.materials[1].uvMatrix == uvMatrix)) return false;
		beam.settings.useWidthCurve = false;
		beam.settings.is2D = false;
		ParticleTrailDataBuilder::Build(beam, beam.settings.blendMode, uvMatrix, layout, data);
		return data.points[3].ribbonT == 0.75f && data.points[2].halfWidth == 2 && data.points[0].side.Length() == 0.0f;
	}

	bool CheckStandaloneTrailGeometry() {

		TrailRendererRecord trail;
		trail.settings.mode = TrailMode::TwoPoints;
		trail.settings.lifetime = 4.0f;
		trail.runtime.time = 3.0;
		trail.runtime.points = {
			{{-1, 0, 0}, {1, 0, 0}, {0, 0, 1}, 0.0},
			{{0, -1, 0}, {0, 1, 0}, {0, 0, 1}, 1.0},
			{{9, 0, 0}, {11, 0, 0}, {0, 0, 1}, 2.0, true},
			{{9, 2, 0}, {11, 2, 0}, {0, 0, 1}, 3.0}
		};
		ParticleCustomParameterLayout layout{};
		layout.stride = 4;
		layout.defaultData = {1, 2, 3, 4};
		ParticleTrailRenderData data;
		ParticleTrailDataBuilder::Build(trail, trail.settings.blendMode, layout, data);
		if (data.points.size() != 4 || data.segments != std::vector<uint32_t>{0, 2} ||
			data.materials.size() != 2 || data.customParameters.size() != 8) return false;
		if (data.points[0].side.x != 1.0f || data.points[1].side.y != 1.0f ||
			data.points[1].halfWidth != 1.0f || data.points[1].ribbonT != 1.0f ||
			data.points[2].ribbonT != 0.0f || data.points[3].color.a <= data.points[0].color.a) return false;
		trail.settings.textureMode = TrailTextureMode::Tile;
		trail.settings.mode = TrailMode::SinglePoint;
		trail.settings.width = 6.0f;
		trail.settings.alignment = TrailAlignment::TransformZ;
		ParticleTrailDataBuilder::Build(trail, trail.settings.blendMode, layout, data);
		if (data.points[3].ribbonT != 2.0f || data.points[3].side.x != 1.0f ||
			data.points[3].halfWidth != 3.0f) return false;
		trail.settings.alignment = TrailAlignment::View;
		ParticleTrailDataBuilder::Build(trail, trail.settings.blendMode, layout, data);
		return data.points[3].side.Length() == 0.0f;
	}
}

bool NEMTests::TestParticleSystemTrails() {

	if (!CheckStandaloneTrailGeometry() || !CheckBeamGeometry()) return false;

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
