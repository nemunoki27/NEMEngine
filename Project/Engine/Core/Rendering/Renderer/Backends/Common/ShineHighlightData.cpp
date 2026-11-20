#include "ShineHighlightData.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderBackend.h>
#include <Engine/Core/Rendering/Textures/RuntimeTextureResolver.h>
#include <Engine/Core/World/Components/Rendering/ShineHighlightComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

#include <cmath>
#include <numbers>

Engine::ShineHighlightData Engine::ResolveShineHighlight(const RenderDrawContext& context, const RenderItem& item) {

	ShineHighlightData data{};
	const auto* shine = item.world ? item.world->TryGetComponent<ShineHighlightComponent>(item.entity) : nullptr;
	if (!shine || !shine->enabled || shine->position <= 0.0f || 1.0f <= shine->position ||
		shine->width <= 0.0f || shine->intensity <= 0.0f) return data;

	const float angle = shine->angle * (std::numbers::pi_v<float> / 180.0f);
	data.direction = {std::cos(angle), -std::sin(angle)};
	data.color = shine->color;
	data.position = shine->position;
	data.width = shine->width;
	data.softness = shine->softness;
	data.intensity = shine->intensity;
	if (context.graphicsCore && shine->rampTexture) {
		const auto ramp = RuntimeTextureResolver::ResolveBindless(*context.graphicsCore,
			context.assetDatabase, shine->rampTexture, TextureColorSpace::SRGB);
		if (ramp.ownership && !ramp.retry) data.rampIndex = ramp.srvIndex;
	}
	return data;
}
