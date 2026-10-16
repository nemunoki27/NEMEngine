#include "LightGPUConversion.h"

// c++
#include <algorithm>
#include <cmath>

Engine::DirectionalLightGPU Engine::LightGPUConversion::ToGPU(const DirectionalLightItem& item) {

	DirectionalLightGPU light{};

	light.color = item.color;
	light.direction = item.direction;
	light.intensity = item.intensity;
	light.shadowStrength = item.shadowStrength;
	light.shadowAngularRadius = item.shadowAngularRadius;

	return light;
}

Engine::PointLightGPU Engine::LightGPUConversion::ToGPU(const PointLightItem& item) {

	PointLightGPU light{};

	light.color = item.color;
	light.pos = item.pos;
	light.intensity = item.intensity;
	light.radius = item.radius;
	light.decay = item.decay;
	light.shadowStrength = item.shadowStrength;
	light.shadowRadius = item.shadowRadius;

	return light;
}

Engine::SpotLightGPU Engine::LightGPUConversion::ToGPU(const SpotLightItem& item) {

	SpotLightGPU light{};

	light.color = item.color;
	light.direction = item.direction;
	light.pos = item.pos;
	light.intensity = item.intensity;
	light.distance = item.distance;
	light.decay = item.decay;
	light.cosAngle = item.cosAngle;
	light.cosFalloffStart = item.cosFalloffStart;
	light.shadowStrength = item.shadowStrength;
	light.shadowRadius = item.shadowRadius;

	return light;
}

Engine::RectLightGPU Engine::LightGPUConversion::ToGPU(const RectLightItem& item) {

	RectLightGPU light{};

	// 矩形の入力範囲を描画とベイクで揃える
	const auto positive = [](float value) { return std::isfinite(value) ? (std::max)(value, 0.0f) : 0.0f; };

	light.color = item.color;
	light.direction = Vector3::Normalize(item.direction);
	if (Vector3::Dot(light.direction, light.direction) < 0.5f) light.direction = Vector3(1.0f, 0.0f, 0.0f);
	light.pos = item.pos;
	light.right = item.right - light.direction * Vector3::Dot(item.right, light.direction);
	if (Vector3::Dot(light.right, light.right) < 1e-8f) {
		light.right = Vector3::Cross(std::abs(light.direction.z) < 0.999f ?
			Vector3(0.0f, 0.0f, 1.0f) : Vector3(0.0f, 1.0f, 0.0f), light.direction);
	}
	light.right = Vector3::Normalize(light.right);
	light.up = Vector3::Cross(light.direction, light.right);
	light.intensity = positive(item.intensity);
	light.attenuationRadius = positive(item.attenuationRadius);
	light.sourceWidth = positive(item.sourceWidth);
	light.sourceHeight = positive(item.sourceHeight);
	light.barnDoorAngle = (std::min)(positive(item.barnDoorAngle), 89.0f);
	light.barnDoorLength = positive(item.barnDoorLength);
	light.shadowStrength = (std::min)(positive(item.shadowStrength), 1.0f);

	return light;
}
