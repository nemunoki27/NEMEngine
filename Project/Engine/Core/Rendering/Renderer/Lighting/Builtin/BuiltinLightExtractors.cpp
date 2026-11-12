#include "BuiltinLightExtractors.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Systems/Lighting/DayNightCycleSystem.h>

// c++
#include <algorithm>
#include <cmath>

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

		DirectionalLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity);
		item.common.influence = light.influence;
		item.color = light.color;
		item.intensity = light.intensity;
		item.shadowStrength = light.shadowStrength;
		item.shadowAngularRadius = light.shadowAngularRadius;
		item.direction = LightExtract::GetDirectionalWorldDirection(world, entity, light.direction);
		Vector3 controlled;
		if (ResolveDayNightLightDirection(world, entity, controlled)) {
			const float radius = std::sin(std::clamp(light.shadowAngularRadius, 0.01f, 45.0f) * 0.0174532925f);
			const float visibility = std::clamp((-controlled.y + radius) / (radius * 2.0f), 0.0f, 1.0f);
			item.intensity *= visibility * visibility * (3.0f - 2.0f * visibility);
		}
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
		LightExtract::FillCommonFields(item.common, world, entity);
		item.common.influence = light.influence;
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
void Engine::BuiltinLightExtractor<Engine::RectLightComponent>::Extract(
	ECSWorld& world, FrameLightBatch& batch) {

	world.ForEach<RectLightComponent>([&](const Entity& entity, RectLightComponent& light) {

		if (!LightExtract::IsVisible(world, entity, light.enabled)) {
			return;
		}

		const Matrix4x4 worldMatrix =
			LightExtract::GetWorldMatrix(world, entity);

		RectLightItem item{};
		LightExtract::FillCommonFields(item.common, world, entity);
		item.common.influence = light.influence;
		item.color = light.color;
		item.pos = worldMatrix.GetTranslationValue();
		item.direction = LightExtract::GetWorldDirection(
			Vector3(1.0f, 0.0f, 0.0f), worldMatrix);
		item.right = LightExtract::GetWorldDirection(
			Vector3(0.0f, 1.0f, 0.0f), worldMatrix);
		item.up = LightExtract::GetWorldDirection(
			Vector3(0.0f, 0.0f, 1.0f), worldMatrix);
		item.intensity = light.intensity;
		item.attenuationRadius = light.attenuationRadius;
		item.sourceWidth = light.sourceWidth;
		item.sourceHeight = light.sourceHeight;
		item.barnDoorAngle = light.barnDoorAngle;
		item.barnDoorLength = light.barnDoorLength;
		item.shadowStrength = light.shadowStrength;
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
		LightExtract::FillCommonFields(item.common, world, entity);
		item.common.influence = light.influence;
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
