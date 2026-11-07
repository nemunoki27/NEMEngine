#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/World/Scene/Utility/SceneEntityReference.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>
#include <Engine/Core/Foundation/Math/Color.h>

namespace Engine {

	//============================================================================
	//	SkyAtmosphereComponent struct
	//	地表から見た空と大気の散乱を設定する
	//============================================================================
	struct SkyAtmosphereComponent {

		static constexpr ComponentChangeChannel kChangeChannels = ComponentChangeChannel::Lighting;
		static constexpr ComponentChangeChannel kTransformChannels = ComponentChangeChannel::Lighting;

		bool enabled = true;
		uint32_t renderingLayerMask = 1u;
		SceneEntityReference sunEntity{};
		float planetRadiusKm = 6360.0f;
		float atmosphereHeightKm = 100.0f;
		Color3 rayleighScattering{0.005802f, 0.013558f, 0.0331f};
		float rayleighScaleHeightKm = 8.0f;
		float mieScattering = 0.003996f;
		float mieAbsorption = 0.000444f;
		float mieScaleHeightKm = 1.2f;
		float mieAnisotropy = 0.8f;
		Color3 ozoneAbsorption{0.000650f, 0.001881f, 0.000085f};
		float ozoneCenterHeightKm = 25.0f;
		float ozoneWidthKm = 30.0f;
		Color3 groundAlbedo{0.3f, 0.3f, 0.3f};
		float iblIntensity = 1.0f;
		float aerialPerspectiveDistance = 32000.0f;
		float aerialPerspectiveStrength = 1.0f;
	};

	void from_json(const nlohmann::json& in, SkyAtmosphereComponent& component);
	void to_json(nlohmann::json& out, const SkyAtmosphereComponent& component);
}
