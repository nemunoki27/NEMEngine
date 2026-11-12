#include "RaytracingSceneBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/LightInfluenceSettings.h>

void Engine::RaytracingSceneBuilder::AppendShadowExclusions(const LightInfluenceSettings& settings,
	const ECSWorld& world, Entity owner, std::vector<uint32_t>& out) const {

	if (settings.shadowExcludedLayerMask == 0u && settings.shadowExclusions.empty()) return;
	const auto exclusions = ResolveLightShadowExclusions(world, owner, settings);
	for (size_t index = 0; index < scene_.shadowInstanceEntities.size(); ++index) {

		const auto& key = scene_.shadowInstanceEntities[index];
		const uint32_t layers = scene_.result.sceneInstanceScratch_[index].renderFlags >> 8u;
		const bool excluded = (layers & settings.shadowExcludedLayerMask) != 0u ||
			(key.world == &world && IsLightShadowExcluded(settings, exclusions, key.entity, 0u));
		if (excluded) out.push_back(static_cast<uint32_t>(index));
	}
}
