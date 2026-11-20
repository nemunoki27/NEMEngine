#include "ParticleRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/BeamRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TrailRendererComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

//============================================================================
//	ParticleRenderItemExtractor classMethods
//============================================================================
void Engine::ParticleRenderItemExtractor::Extract(
	ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<ParticleSystemComponent, ParticleSystemRuntimeComponent>(
		[&](const Entity& entity, const ParticleSystemComponent& component,
			const ParticleSystemRuntimeComponent& storage) {

			if (!RenderItemExtract::IsVisible(world, entity, component.visible)) {
				return;
			}
			const ParticleSystemRuntimeData* runtime =
				TryGetParticleSystemRuntime(world, entity);
			if (!runtime) {
				return;
			}
			if (!runtime->instance.definition) return;
			const ParticleEmitterRuntime& group = runtime->instance.emitter;

			ParticleRenderPayload payload{};
			payload.runtimeIndex = storage.handle.index;
			payload.runtimeGeneration = storage.handle.generation;

			RenderItem item{};
			item.entity = entity;
			item.renderSettings = component.renderSettings;
			item.world = &world;
			item.sceneInstanceID =
				SceneObjectUtility::GetSceneInstanceID(world, entity);
			item.renderPhase = group.renderSettings.queue;
			// 所有Rendererの可視レイヤーを適用する
			item.visibilityLayerMask = RenderItemExtract::GetVisibilityLayerMask(
				world, entity, component.renderingLayerMask);
			item.sortingLayer = component.layer;
			item.sortingOrder = component.order;
			item.blendMode = group.renderSettings.blendMode;
			item.renderingLayerMask =
				component.renderingLayerMask & kRenderingLayerMaskBits;
			item.worldMatrix = RenderItemExtract::GetWorldMatrix(world, entity);
			item.backendID = RenderBackendID::Particle;
			item.batchKey = static_cast<uint64_t>(entity.index) << 2;
			item.cameraDomain = RenderCameraDomain::Perspective;
			if (group.renderSettings.space == PrimitiveRenderSpace::Screen2D) {
				item.cameraDomain = RenderCameraDomain::Orthographic;
			}
			item.payload = batch.PushPayload(payload);
			batch.Add(std::move(item));
		});

	const auto addRibbon = [&](Entity entity, const auto* ribbon, ParticleRenderSource source) {

		if (!ribbon || !RenderItemExtract::IsVisible(world, entity, ribbon->settings.visible)) return;

		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, ribbon->settings,
			RenderItemExtract::GetWorldMatrix(world, entity));
		// リボンのPSは最終色のみを出力するため、ワールドではライティング後に合成する
		item.surfaceMode = MaterialSurfaceMode::Transparent;
		item.surfaceModeOverridden = true;
		item.renderPhase = ResolveMaterialRenderPhase(item.surfaceMode, item.renderPhase);
		item.backendID = RenderBackendID::Particle;
		item.cameraDomain = ribbon->settings.is2D ? RenderCameraDomain::Orthographic : RenderCameraDomain::Perspective;
		item.material = ribbon->settings.material;
		item.batchKey = (static_cast<uint64_t>(entity.index) << 2) | static_cast<uint64_t>(source);
		ParticleRenderPayload payload{};
		payload.source = source;
		item.payload = batch.PushPayload(payload);
		batch.Add(std::move(item));
	};
	const ECSWorld& source = world;
	world.ForEach<TrailRendererComponent>([&](const Entity& entity, const TrailRendererComponent&) {
		addRibbon(entity, TryGetTrailRenderer(source, entity), ParticleRenderSource::Trail);
	});
	world.ForEach<BeamRendererComponent>([&](const Entity& entity, const BeamRendererComponent&) {
		addRibbon(entity, TryGetBeamRenderer(source, entity), ParticleRenderSource::Beam);
	});
}
