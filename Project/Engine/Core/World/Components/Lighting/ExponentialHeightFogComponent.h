#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	ExponentialHeightFogComponent struct
	//	GameObjectの高さを基準にした二層の高さ霧
	//============================================================================
	struct ExponentialHeightFogComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Lighting;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::Lighting;

		bool enabled = true;
		uint32_t renderingLayerMask = 1u;
		float density = 0.002f;
		float heightFalloff = 0.02f;
		Color3 color{0.5f, 0.6f, 0.7f};
		float startDistance = 0.0f;
		float maxOpacity = 1.0f;
		float cutoffDistance = 0.0f;
		Color3 directionalColor{1.0f, 1.0f, 1.0f};
		float directionalExponent = 8.0f;
		float directionalStartDistance = 0.0f;
		float directionalIntensity = 0.0f;
		float skyContribution = 1.0f;
		float secondDensity = 0.0f;
		float secondHeightFalloff = 0.02f;
		float secondHeightOffset = 0.0f;
	};

	void from_json(const nlohmann::json& in, ExponentialHeightFogComponent& component);
	void to_json(nlohmann::json& out, const ExponentialHeightFogComponent& component);
}
