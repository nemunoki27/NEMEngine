#include "BuiltinLightExtractors.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>

//============================================================================
//	BuiltinLightExtractor specializations
//============================================================================
template <>
void Engine::BuiltinLightExtractor<Engine::DirectionalLightComponent>::Extract(
	ECSWorld& world, FrameLightBatch& batch) {

	world.ForEach<DirectionalLightComponent>([&](const Entity& entity, DirectionalLightComponent& light) {

		if (!LightExtract::IsVisible(world, entity, light.enabled)) {
			return;
		}

		const Matrix4x4 worldMatrix = LightExtract::GetWorldMatrix(world, entity);

		DirectionalLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity, light);
		item.color = light.color;
		item.intensity = light.intensity;
		item.shadowStrength = light.shadowStrength;
		item.shadowAngularRadius = light.shadowAngularRadius;
		item.direction = LightExtract::GetWorldDirection(light.direction, worldMatrix);
		batch.Add(std::move(item));
		});
}

template <>
void Engine::BuiltinLightExtractor<Engine::PointLightComponent>::Extract(
	ECSWorld& world, FrameLightBatch& batch) {

	world.ForEach<PointLightComponent>([&](const Entity& entity, PointLightComponent& light) {

		if (!LightExtract::IsVisible(world, entity, light.enabled)) {
			return;
		}

		PointLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity, light);
		item.color = light.color;
		item.pos = LightExtract::GetWorldPos(world, entity);
		item.intensity = light.intensity;
		item.radius = light.radius;
		item.decay = light.decay;
		item.shadowStrength = light.shadowStrength;
		item.shadowRadius = light.shadowRadius;
		batch.Add(std::move(item));
		});
}

template <>
void Engine::BuiltinLightExtractor<Engine::SpotLightComponent>::Extract(
	ECSWorld& world, FrameLightBatch& batch) {

	world.ForEach<SpotLightComponent>([&](const Entity& entity, SpotLightComponent& light) {

		if (!LightExtract::IsVisible(world, entity, light.enabled)) {
			return;
		}

		const Matrix4x4 worldMatrix = LightExtract::GetWorldMatrix(world, entity);

		SpotLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity, light);
		item.color = light.color;
		item.pos = worldMatrix.GetTranslationValue();
		item.direction = LightExtract::GetWorldDirection(light.direction, worldMatrix);
		item.intensity = light.intensity;
		item.distance = light.distance;
		item.decay = light.decay;
		item.cosAngle = light.cosAngle;
		item.cosFalloffStart = light.cosFalloffStart;
		item.shadowStrength = light.shadowStrength;
		item.shadowRadius = light.shadowRadius;
		batch.Add(std::move(item));
		});
}
