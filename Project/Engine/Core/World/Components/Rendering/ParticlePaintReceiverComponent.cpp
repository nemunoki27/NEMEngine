#include "ParticlePaintReceiverComponent.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>
#include <cmath>

void Engine::ClearParticlePaint(ECSWorld& world, const Entity& entity) {

	if (auto* receiver = world.TryGetComponent<ParticlePaintReceiverComponent>(entity)) ++receiver->clearSerial;
}

void Engine::from_json(const nlohmann::json& in, ParticlePaintReceiverComponent& component) {

	component.enabled = in.value("enabled", component.enabled);
	component.projection = EnumAdapter<ParticlePaintProjection>::FromString(in.value("projection", "PlanarXZ"))
		.value_or(ParticlePaintProjection::PlanarXZ);
	component.resolution = std::clamp(in.value("resolution", component.resolution), 32u, 2048u);
	if (in.contains("planarSize")) component.planarSize = Vector2::FromJson(in["planarSize"]);
	component.planarSize.x = std::isfinite(component.planarSize.x) ? std::max(0.001f, component.planarSize.x) : 10.0f;
	component.planarSize.y = std::isfinite(component.planarSize.y) ? std::max(0.001f, component.planarSize.y) : 10.0f;
	component.opacity = in.value("opacity", component.opacity);
	component.opacity = std::isfinite(component.opacity) ? std::clamp(component.opacity, 0.0f, 1.0f) : 1.0f;
}

void Engine::to_json(nlohmann::json& out, const ParticlePaintReceiverComponent& component) {

	out = {{"enabled", component.enabled}, {"projection", EnumAdapter<ParticlePaintProjection>::ToString(component.projection)},
		{"resolution", component.resolution}, {"planarSize", component.planarSize.ToJson()}, {"opacity", component.opacity}};
}
