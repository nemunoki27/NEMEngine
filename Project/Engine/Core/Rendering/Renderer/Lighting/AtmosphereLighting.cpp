#include "AtmosphereLighting.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Backends/Core/IRenderItemExtractor.h>
#include <Engine/Core/Rendering/Renderer/Lighting/Interface/ILightExtractor.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Systems/Lighting/DayNightCycleSystem.h>

// c++
#include <algorithm>
#include <cmath>

Engine::SkyAtmosphereComponent Engine::AtmosphereLighting::Sanitize(const SkyAtmosphereComponent& source) {

	SkyAtmosphereComponent sky = source;
	const SkyAtmosphereComponent defaults{};
	auto finite = [](float value, float fallback, float minimum, float maximum) {
		return std::isfinite(value) ? std::clamp(value, minimum, maximum) : fallback;
	};
	auto color = [&](Color3 value, Color3 fallback, float maximum) {
		return Color3(finite(value.r, fallback.r, 0.0f, maximum), finite(value.g, fallback.g, 0.0f, maximum),
			finite(value.b, fallback.b, 0.0f, maximum));
	};
	sky.planetRadiusKm = finite(sky.planetRadiusKm, defaults.planetRadiusKm, 1.0f, 100000.0f);
	sky.atmosphereHeightKm = finite(sky.atmosphereHeightKm, defaults.atmosphereHeightKm, 0.1f, 10000.0f);
	sky.rayleighScattering = color(sky.rayleighScattering, defaults.rayleighScattering, 10.0f);
	sky.rayleighScaleHeightKm = finite(sky.rayleighScaleHeightKm, defaults.rayleighScaleHeightKm, 0.001f, 10000.0f);
	sky.mieScattering = finite(sky.mieScattering, defaults.mieScattering, 0.0f, 10.0f);
	sky.mieAbsorption = finite(sky.mieAbsorption, defaults.mieAbsorption, 0.0f, 10.0f);
	sky.mieScaleHeightKm = finite(sky.mieScaleHeightKm, defaults.mieScaleHeightKm, 0.001f, 10000.0f);
	sky.mieAnisotropy = finite(sky.mieAnisotropy, defaults.mieAnisotropy, -0.95f, 0.95f);
	sky.ozoneAbsorption = color(sky.ozoneAbsorption, defaults.ozoneAbsorption, 10.0f);
	sky.ozoneCenterHeightKm = finite(sky.ozoneCenterHeightKm, defaults.ozoneCenterHeightKm, 0.0f, 10000.0f);
	sky.ozoneWidthKm = finite(sky.ozoneWidthKm, defaults.ozoneWidthKm, 0.001f, 10000.0f);
	sky.groundAlbedo = color(sky.groundAlbedo, defaults.groundAlbedo, 1.0f);
	sky.iblIntensity = finite(sky.iblIntensity, defaults.iblIntensity, 0.0f, 10000.0f);
	sky.aerialPerspectiveDistance = finite(sky.aerialPerspectiveDistance, defaults.aerialPerspectiveDistance, 0.01f, 10000000.0f);
	sky.aerialPerspectiveStrength = finite(sky.aerialPerspectiveStrength, defaults.aerialPerspectiveStrength, 0.0f, 100.0f);
	sky.moonAngularRadius = finite(sky.moonAngularRadius, defaults.moonAngularRadius, 0.01f, 45.0f);
	sky.moonDiskColor = color(sky.moonDiskColor, defaults.moonDiskColor, 100.0f);
	sky.moonDiskBrightness = finite(sky.moonDiskBrightness, defaults.moonDiskBrightness, 0.0f, 10000.0f);
	return sky;
}

const Engine::SkyAtmosphereComponent* Engine::AtmosphereLighting::Find(ECSWorld& world,
	uint32_t cullingMask, Entity& owner) {

	const SkyAtmosphereComponent* result = nullptr;
	world.ForEach<SkyAtmosphereComponent>([&](Entity entity, const SkyAtmosphereComponent& sky) {
		if (result || !RenderItemExtract::IsVisible(world, entity, sky.enabled) ||
			(RenderItemExtract::GetVisibilityLayerMask(world, entity, sky.renderingLayerMask) & cullingMask) == 0) return;
		result = &sky;
		owner = entity;
	});
	return result;
}

Engine::Entity Engine::AtmosphereLighting::ResolveSun(ECSWorld& world, Entity owner, const SkyAtmosphereComponent& sky) {

	Entity controlledSun{}, controlledMoon{};
	if (ResolveDayNightAtmosphereLights(world, owner, controlledSun, controlledMoon)) return controlledSun;
	Entity sun = sky.sunEntity.Resolve(world, owner);
	if (!sky.sunEntity.localFileID) {
		world.ForEach<DirectionalLightComponent>([&](Entity entity, const auto& light) {
			if (!sun.IsValid() && LightExtract::IsVisible(world, entity, light.enabled)) sun = entity;
		});
	}
	return sun;
}

Engine::Entity Engine::AtmosphereLighting::ResolveMoon(ECSWorld& world, Entity owner, const SkyAtmosphereComponent& sky) {

	Entity sun{}, moon{};
	if (ResolveDayNightAtmosphereLights(world, owner, sun, moon)) return moon;
	const Entity configured = sky.moonEntity.Resolve(world, owner);
	return configured != ResolveSun(world, owner, sky) ? configured : Entity::Null();
}

Engine::Vector3 Engine::AtmosphereLighting::SunTransmittance(const SkyAtmosphereComponent& source,
	float altitudeKm, const Vector3& direction) {

	const SkyAtmosphereComponent sky = Sanitize(source);
	if (!std::isfinite(altitudeKm)) altitudeKm = 0.0f;
	const double vertical = std::isfinite(direction.y) ? std::clamp(double(direction.y), -1.0, 1.0) : 1.0;
	const double groundRadius = sky.planetRadiusKm;
	const double height = groundRadius + std::max(double(altitudeKm), 0.002);
	const double top = groundRadius + sky.atmosphereHeightKm;
	const double b = height * vertical;
	const double groundDiscriminant = b * b - height * height + groundRadius * groundRadius;
	if (b < 0.0 && groundDiscriminant >= 0.0) return Vector3::AnyInit(0.0f);
	const double discriminant = b * b - height * height + top * top;
	if (discriminant < 0.0) return Vector3::AnyInit(1.0f);
	const double distance = std::max(-b + std::sqrt(discriminant), 0.0);
	const double step = distance / 40.0;
	double rayleigh = 0.0, mie = 0.0, ozone = 0.0;
	for (uint32_t i = 0; i < 40; ++i) {
		const double t = (double(i) + 0.5) * step;
		const double altitude = std::max(std::sqrt(height * height + t * t + 2.0 * b * t) - groundRadius, 0.0);
		rayleigh += std::exp(-altitude / std::max(double(sky.rayleighScaleHeightKm), 0.001)) * step;
		mie += std::exp(-altitude / std::max(double(sky.mieScaleHeightKm), 0.001)) * step;
		ozone += std::clamp(1.0 - std::abs(altitude - sky.ozoneCenterHeightKm) /
			std::max(double(sky.ozoneWidthKm) * 0.5, 0.001), 0.0, 1.0) * step;
	}
	const double mieDepth = (sky.mieScattering + sky.mieAbsorption) * mie;
	return {float(std::exp(-sky.rayleighScattering.r * rayleigh - mieDepth - sky.ozoneAbsorption.r * ozone)),
		float(std::exp(-sky.rayleighScattering.g * rayleigh - mieDepth - sky.ozoneAbsorption.g * ozone)),
		float(std::exp(-sky.rayleighScattering.b * rayleigh - mieDepth - sky.ozoneAbsorption.b * ozone))};
}
