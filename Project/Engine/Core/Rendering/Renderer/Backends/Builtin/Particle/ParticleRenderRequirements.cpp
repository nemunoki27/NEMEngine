#include "ParticleRenderRequirements.h"

//============================================================================
//	include
//============================================================================
#include "ParticleRenderDataUtility.h"
#include <Engine/Core/Rendering/Renderer/Queues/RenderQueue.h>
#include <Engine/Core/Rendering/Particle/ParticleGPUState.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

namespace {

	// 描画アイテムから受光設定を取得する
	const Engine::ParticleRenderSettings* ResolveSettings(const Engine::RenderSceneBatch& batch, const Engine::RenderItem& item) {

		if (item.backendID != Engine::RenderBackendID::Particle) return nullptr;
		const auto* payload = batch.GetPayload<Engine::ParticleRenderPayload>(item);
		const auto* emitter = payload ? Engine::ResolveParticleRenderEmitter(item, *payload) : nullptr;
		if (!emitter || !emitter->gpu || emitter->gpu->rejected ||
			emitter->renderSettings.space != Engine::PrimitiveRenderSpace::World3D) return nullptr;
		return &emitter->renderSettings;
	}
}

bool Engine::ParticleRenderRequirements::RequiresOpaqueDepth(const RenderSceneBatch& batch,
	std::span<const RenderItem* const> items, Entity camera, Entity mainCamera) {

	for (const auto* item : items) {
		if (!item) continue;
		const auto* settings = ResolveSettings(batch, *item);
		if (settings && settings->surface.softParticles) return true;
		if (settings && settings->authoringOwner && camera.IsValid()) {
			const auto& source = settings->authoringOwner->source;
			if (source.kind == ParticleSourceKind::CameraSurface) {
				const auto selected = source.camera.localFileID ? source.camera.Resolve(*item->world, item->entity) : mainCamera;
				if (selected == camera) return true;
			}
		}
	}
	return false;
}

bool Engine::ParticleRenderRequirements::RequiresDirectionalShadows(const RenderSceneBatch& batch,
	std::span<const RenderItem* const> items) {

	for (const auto* item : items) {
		if (!item) continue;
		const auto* settings = ResolveSettings(batch, *item);
		if (settings && settings->surface.lit && settings->surface.receiveShadows) return true;
	}
	return false;
}
