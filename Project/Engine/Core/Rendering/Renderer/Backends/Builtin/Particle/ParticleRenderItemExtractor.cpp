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

		// 描画可能か、粒子が無ければ抽出しない
		if (!RenderItemExtract::IsVisible(world, entity, emitter.visible)) {
			return;
		}
		if (emitter.runtimeParticles.empty()) {
			return;
		}

		// ペイロード構築
		ParticleRenderPayload payload{};
		payload.emitter = &emitter;

		// 描画アイテムの構築、粒子配列はエンティティごとなのでバッチはエンティティ単位で分離する
		RenderItem item{};
		RenderItemExtract::FillCommonFields(item, world, entity, emitter, RenderItemExtract::GetWorldMatrix(world, entity));
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
