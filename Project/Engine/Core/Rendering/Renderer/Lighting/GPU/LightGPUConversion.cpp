#include "LightGPUConversion.h"

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

	light.color = item.color;
	light.direction = item.direction;
	light.pos = item.pos;
	light.right = item.right;
	light.up = item.up;
	light.intensity = item.intensity;
	light.attenuationRadius = item.attenuationRadius;
	light.sourceWidth = item.sourceWidth;
	light.sourceHeight = item.sourceHeight;
	light.decay = item.decay;
	light.barnDoorAngle = item.barnDoorAngle;
	light.barnDoorLength = item.barnDoorLength;
	light.shadowStrength = item.shadowStrength;

	return light;
}
