#include "ParticleRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
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
			item.batchKey = static_cast<uint64_t>(entity.index + 1) *
				0x9E3779B97F4A7C15ull ^ storage.handle.generation;
			item.cameraDomain = RenderCameraDomain::Perspective;
			if (group.renderSettings.space == PrimitiveRenderSpace::Screen2D) {
				item.cameraDomain = RenderCameraDomain::Orthographic;
			}
			item.payload = batch.PushPayload(payload);
			batch.Add(std::move(item));
		});
}
