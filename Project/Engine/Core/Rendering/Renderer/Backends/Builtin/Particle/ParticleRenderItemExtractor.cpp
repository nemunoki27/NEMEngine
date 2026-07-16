#include "ParticleRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/EffectEmitterComponent.h>

//============================================================================
//	ParticleRenderItemExtractor classMethods
//============================================================================
void Engine::ParticleRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<EffectEmitterComponent>([&](const Entity& entity, const EffectEmitterComponent& emitter) {

		// 描画可能か、粒子と残存トレイルが無ければ抽出しない
		if (!RenderItemExtract::IsVisible(world, entity, emitter.visible)) {
			return;
		}
		for (const EffectEmitterPlaybackRuntime& playback : emitter.runtimePlaybacks) {
			for (const EffectEmitterStateRuntime& state : playback.states) {
				for (const ParticleEffectInstanceRuntime& effect : state.effects) {
					for (const ParticleGroupRuntimeState& group : effect.runtimeGroups) {

						if (group.particles.empty() && group.trails.empty()) { continue; }

						// ペイロード構築
						ParticleRenderPayload payload{};
						payload.group = &group;

						// グループごとの描画設定で描画アイテムを構築する
						RenderItem item{};
						item.entity = entity;
						item.world = &world;
						item.sceneInstanceID = SceneObjectUtility::GetSceneInstanceID(world, entity);
						item.renderPhase = group.renderSettings.queue;
						if (const SceneObjectComponent* sceneObject = RenderItemExtract::GetSceneObject(world, entity)) {
							item.visibilityLayerMask = sceneObject->visibilityLayerMask;
						}
						item.sortingLayer = emitter.layer;
						item.sortingOrder = emitter.order;
						item.blendMode = group.renderSettings.blendMode;
						item.worldMatrix = RenderItemExtract::GetWorldMatrix(world, entity);
						item.backendID = RenderBackendID::Particle;
						item.batchKey = static_cast<uint64_t>(entity.index + 1) * 0x9E3779B97F4A7C15ull ^
							effect.id * 0xBF58476D1CE4E5B9ull ^ group.groupID.value;
						item.cameraDomain = RenderCameraDomain::Perspective;
						// 2Dエフェクトは正射投影のScreenUIフェーズへ流す
						if (group.renderSettings.space == PrimitiveRenderSpace::Screen2D) {
							item.cameraDomain = RenderCameraDomain::Orthographic;
							item.renderPhase = RenderPhase::ScreenUI;
						}
						item.payload = batch.PushPayload(payload);
						batch.Add(std::move(item));
					}
				}
			}
		}
		});
}
