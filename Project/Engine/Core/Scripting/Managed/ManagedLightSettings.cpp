#include "ManagedLightSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

Engine::LightInfluenceSettings* Engine::ResolveLightInfluenceSettings(ECSWorld& world, Entity entity, int32_t type) {

	switch (type) {
	case 7: if (auto* light = world.TryGetComponentForBinding<DirectionalLightComponent>(entity)) return &light->influence; break;
	case 18: if (auto* light = world.TryGetComponentForBinding<PointLightComponent>(entity)) return &light->influence; break;
	case 28: if (auto* light = world.TryGetComponentForBinding<SpotLightComponent>(entity)) return &light->influence; break;
	case 59: if (auto* light = world.TryGetComponentForBinding<RectLightComponent>(entity)) return &light->influence; break;
	}
	return nullptr;
}
