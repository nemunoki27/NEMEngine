#include "TrailRendererTests.h"

#include <Engine/Core/Rendering/Renderer/Backends/Builtin/Particle/ParticleRenderItemExtractor.h>
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Time/TimeScaleComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Rendering/TrailRendererSystem.h>

bool NEMTests::TestTrailRenderer() {

	using namespace Engine;
	ECSWorld world;
	const Entity owner = world.CreateEntity();
	world.AddComponent<TransformComponent>(owner);
	world.AddComponent<TrailRendererComponent>(owner);
	world.AddComponent<TimeScaleComponent>(owner);
	auto* record = TryGetTrailRenderer(world, owner);
	if (!record || !record->settings.material || record->settings.widthCurve.Evaluate(0.5f) != 1) return false;
	ParticleRenderItemExtractor extractor;
	for (const auto phase : {RenderPhase::Transparent, RenderPhase::Opaque, RenderPhase::ScreenUI}) {
		record->settings.queue = phase;
		record->settings.blendMode = BlendMode::Add;
		RenderSceneBatch batch;
		extractor.Extract(world, batch);
		if (batch.GetItems().size() != 1) return false;
		const auto& item = batch.GetItems().front();
		if (!item.surfaceModeOverridden || item.surfaceMode != MaterialSurfaceMode::Transparent ||
			item.renderPhase != (phase == RenderPhase::ScreenUI ? phase : RenderPhase::Transparent) ||
			ResolveMaterialBlendMode(item.surfaceMode, item.blendMode) != BlendMode::Add) return false;
	}
	TrailRendererSystem system;
	SystemContext context;
	context.mode = WorldMode::Play;
	context.deltaTime = context.unscaledDeltaTime = 0.02f;
	system.LateUpdate(world, context);
	auto& matrix = world.GetComponent<TransformComponent>(owner).worldMatrix;
	matrix.m[3][0] = 0.01f;
	system.LateUpdate(world, context);
	if (record->runtime.points.size() != 1) return false;
	matrix.m[3][0] = 1;
	system.LateUpdate(world, context);
	if (record->runtime.points.size() != 2 || record->runtime.points.back().breakBefore) return false;
	world.GetComponent<TimeScaleComponent>(owner).timeScale = 0;
	const double time = record->runtime.time;
	system.LateUpdate(world, context);
	if (record->runtime.time != time) return false;
	world.GetComponent<TimeScaleComponent>(owner).timeScale = 1;
	SetTrailEmitting(world, owner, false);
	AddTrailPosition(world, owner, {2, 0, 0});
	matrix.m[3][0] = 10;
	system.LateUpdate(world, context);
	AddTrailPosition(world, owner, {3, 0, 0});
	if (record->runtime.points.size() != 4 || record->runtime.points.back().breakBefore) return false;
	SetTrailEmitting(world, owner, true);
	system.LateUpdate(world, context);
	if (record->runtime.points.size() != 5 || !record->runtime.points.back().breakBefore) return false;
	ClearTrail(world, owner);
	auto settings = record->settings;
	settings.mode = TrailMode::TwoPoints;
	SetTrailRendererSettings(world, owner, settings);
	const Entity root = world.CreateEntity(), tip = world.CreateEntity();
	world.AddComponent<TransformComponent>(root);
	world.AddComponent<TransformComponent>(tip);
	record->runtime.rootOverride = root;
	record->runtime.tipOverride = tip;
	world.GetComponent<TransformComponent>(tip).worldMatrix.m[3][0] = 1;
	system.LateUpdate(world, context);
	world.GetComponent<TransformComponent>(tip).worldMatrix.m[3][1] = 1;
	system.LateUpdate(world, context);
	if (record->runtime.points.size() != 2 || record->runtime.points.back().tip != Vector3(1, 1, 0)) return false;
	auto cloned = world.CloneForSerialization();
	const auto* copy = TryGetTrailRenderer(*cloned, cloned->FindByUUID(world.GetUUID(owner)));
	if (!copy || copy->settings.mode != TrailMode::TwoPoints || !copy->runtime.points.empty() ||
		copy->runtime.rootOverride.IsValid() || copy->runtime.tipOverride.IsValid()) return false;
	world.DestroyEntity(tip);
	system.LateUpdate(world, context);
	if (!record->runtime.breakBefore || record->runtime.points.size() != 2) return false;
	AgeTrail(*record, 1.0);
	if (!record->runtime.points.empty()) return false;
	record->settings.is2D = true;
	AppendTrailSample(*record, {0, 1, 4}, {2, 3, 4}, {1, 0, 0});
	AppendTrailSample(*record, {0, 1, 6}, {2, 3, 6}, {1, 0, 0});
	const auto& projected = record->runtime.points.back();
	return record->runtime.points.size() == 1 && projected.root.z == 0 && projected.tip.z == 0 &&
		projected.normal == Vector3(0, 0, 1);
}
