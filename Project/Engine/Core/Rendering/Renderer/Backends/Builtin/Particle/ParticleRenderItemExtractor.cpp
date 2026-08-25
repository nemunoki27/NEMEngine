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
			const ParticleSystemRuntimeComponent&) {

			if (!RenderItemExtract::IsVisible(world, entity, component.visible)) {
				return;
			}
			const ParticleSystemRuntimeData* runtime =
				TryGetParticleSystemRuntime(world, entity);
			if (!runtime) {
				return;
			}
			const std::vector<ParticleGroupRuntimeState>& groups =
				runtime->effect.runtimeGroups;
			for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex) {

				const ParticleGroupRuntimeState& group = groups[groupIndex];

				ParticleRenderPayload payload{};
				payload.groupID = group.groupID;
				payload.groupIndex = static_cast<uint32_t>(groupIndex);

				RenderItem item{};
				item.entity = entity;
				item.world = &world;
				item.sceneInstanceID =
					SceneObjectUtility::GetSceneInstanceID(world, entity);
				item.renderPhase = group.renderSettings.queue;
				if (const SceneObjectComponent* sceneObject =
					RenderItemExtract::GetSceneObject(world, entity)) {
					item.visibilityLayerMask = sceneObject->visibilityLayerMask;
				}
				item.sortingLayer = component.layer;
				item.sortingOrder = component.order;
				item.blendMode = group.renderSettings.blendMode;
				item.renderingLayerMask =
					group.renderSettings.renderingLayerMask &
					kRenderingLayerMaskBits;
				item.worldMatrix = RenderItemExtract::GetWorldMatrix(world, entity);
				item.backendID = RenderBackendID::Particle;
				item.batchKey = static_cast<uint64_t>(entity.index + 1) *
					0x9E3779B97F4A7C15ull ^ group.groupID.value;
				item.cameraDomain = RenderCameraDomain::Perspective;
				if (group.renderSettings.space == PrimitiveRenderSpace::Screen2D) {
					item.cameraDomain = RenderCameraDomain::Orthographic;
				}
				item.payload = batch.PushPayload(payload);
				batch.Add(std::move(item));
			}
		});
}
