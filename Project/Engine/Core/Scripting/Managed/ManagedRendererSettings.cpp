#include "ManagedRendererSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>

namespace {

	template<class T>
	Engine::RendererRenderSettings* GetSettings(Engine::ECSWorld& world, Engine::Entity entity) {

		auto* component = world.TryGetComponentForBinding<T>(entity);
		return component ? &component->renderSettings : nullptr;
	}
}

Engine::RendererRenderSettings* Engine::ResolveRendererSettings(ECSWorld& world, Entity entity, int32_t type) {

	switch (type) {
	case 8: return GetSettings<ParticleSystemComponent>(world, entity);
	case 13: return GetSettings<LineRendererComponent>(world, entity);
	case 14: return GetSettings<MeshRendererComponent>(world, entity);
	case 20: return GetSettings<PrimitiveRendererComponent>(world, entity);
	case 29: return GetSettings<SpriteRendererComponent>(world, entity);
	case 30: return GetSettings<TextRendererComponent>(world, entity);
	default: return nullptr;
	}
}
