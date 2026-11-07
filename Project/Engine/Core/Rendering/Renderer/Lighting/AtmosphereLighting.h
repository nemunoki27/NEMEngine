#pragma once
#include <Engine/Core/World/Components/Lighting/SkyAtmosphereComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Engine::AtmosphereLighting {

	SkyAtmosphereComponent Sanitize(const SkyAtmosphereComponent& sky);
	const SkyAtmosphereComponent* Find(ECSWorld& world, uint32_t cullingMask, Entity& owner);
	Entity ResolveSun(ECSWorld& world, Entity owner, const SkyAtmosphereComponent& sky);
	Vector3 SunTransmittance(const SkyAtmosphereComponent& sky, float altitudeKm, const Vector3& direction);
}
