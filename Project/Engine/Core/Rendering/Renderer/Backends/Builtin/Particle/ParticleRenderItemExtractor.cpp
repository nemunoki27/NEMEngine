#include "ParticleRenderItemExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Rendering/ParticleEmitterComponent.h>

//============================================================================
//	ParticleRenderItemExtractor classMethods
//============================================================================
void Engine::ParticleRenderItemExtractor::Extract(ECSWorld& world, RenderSceneBatch& batch) {

	world.ForEach<ParticleEmitterComponent>([&](const Entity& entity, const ParticleEmitterComponent& emitter) {

		// 描画可能か、粒子と残存トレイルが無ければ抽出しない
		if (!RenderItemExtract::IsVisible(world, entity, emitter.visible)) {
			return;
		}
		if (emitter.runtimeParticles.empty() && emitter.runtimeTrails.empty()) {
			return;
		}

		// ペイロード構築
		ParticleRenderPayload payload{};
		payload.emitter = &emitter;

		// 描画アイテムの構築、粒子配列はエンティティごとなのでバッチはエンティティ単位で分離する
		RenderItem item{};
		item.entity = entity;
		item.world = &world;
		item.sceneInstanceID = SceneObjectUtility::GetSceneInstanceID(world, entity);
		item.renderPhase = emitter.runtimeRenderSettings.queue;
		if (const SceneObjectComponent* sceneObject = RenderItemExtract::GetSceneObject(world, entity)) {
			item.visibilityLayerMask = sceneObject->visibilityLayerMask;
		}
		item.sortingLayer = emitter.layer;
		item.sortingOrder = emitter.order;
		item.blendMode = emitter.runtimeRenderSettings.blendMode;
		item.worldMatrix = RenderItemExtract::GetWorldMatrix(world, entity);
		item.backendID = RenderBackendID::Particle;
		item.batchKey = static_cast<uint64_t>(entity.index + 1) * 0x9E3779B97F4A7C15ull;
		item.cameraDomain = RenderCameraDomain::Perspective;
		// 2Dエフェクトは正射投影のScreenUIフェーズへ流す
		if (emitter.runtimeRenderSettings.space == PrimitiveRenderSpace::Screen2D) {
			item.cameraDomain = RenderCameraDomain::Orthographic;
			item.renderPhase = RenderPhase::ScreenUI;
		}
		item.payload = batch.PushPayload(payload);
		batch.Add(std::move(item));
		});
}
