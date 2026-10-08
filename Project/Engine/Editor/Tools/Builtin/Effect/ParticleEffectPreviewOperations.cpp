#include "ParticleEffectPreviewOperations.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>

namespace Engine::ParticleEffectPreviewOperations {

	bool UsesEffect(const Engine::ParticleSystemComponent& component,
		Engine::AssetID effectID) {

		const Engine::AssetID resolved = component.effect ? component.effect :
			Engine::BuiltinAssets::Effects::DefaultParticle;
		return resolved == effectID;
	}

	const Engine::ParticleEffectInstanceRuntime* ResolveEffectInstance(
		const Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::ParticleSystemComponent& component,
		Engine::AssetID effectID) {

		if (!UsesEffect(component, effectID)) {
			return nullptr;
		}
		const Engine::ParticleSystemRuntimeData* runtime =
			Engine::TryGetParticleSystemRuntime(world, entity);
		return runtime ? &runtime->effect : nullptr;
	}
}

void Engine::ParticleEffectPreviewOperations::RestartParticleSystems(
	const EditorToolContext& context, AssetID effectID, std::string& statusMessage, bool oneShot) {

	ECSWorld* world = context.GetWorld();
	if (!world) {

		statusMessage = "再生対象のシーンがありません";
		return;
	}
	// 対象エフェクトを使っているParticleSystemを頭から再生する
	size_t restartCount = 0;
	world->ForEach<ParticleSystemComponent>(
		[&](const Entity& entity, const ParticleSystemComponent& component) {

		if (!UsesEffect(component, effectID)) { return; }
		RequestParticleSystemRestart(*world, entity, oneShot);
		++restartCount;
		});
	statusMessage = 0 < restartCount ?
		std::string{} : "編集中のエフェクトを使用するParticleSystemがありません";
}

void Engine::ParticleEffectPreviewOperations::StopParticleSystems(
	const EditorToolContext& context, AssetID effectID, std::string& statusMessage) {

	ECSWorld* world = context.GetWorld();
	if (!world) {

		statusMessage = "停止対象のシーンがありません";
		return;
	}
	// 対象エフェクトを使っているParticleSystemを停止して粒子を消す
	size_t stopCount = 0;
	world->ForEach<ParticleSystemComponent>(
		[&](const Entity& entity, const ParticleSystemComponent& component) {

		if (!UsesEffect(component, effectID)) { return; }
		RequestParticleSystemStop(*world, entity,
			ParticleSystemStopBehavior::StopEmittingAndClear);
		++stopCount;
		});
	statusMessage = 0 < stopCount ?
		std::string{} : "編集中のエフェクトを使用するParticleSystemがありません";
}
