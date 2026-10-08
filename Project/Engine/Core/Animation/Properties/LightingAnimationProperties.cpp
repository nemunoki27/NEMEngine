#include "BuiltinAnimationPropertyGroups.h"

//============================================================================
//	include
//============================================================================
#include "BuiltinAnimationPropertyUtility.h"
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>

using namespace Engine::AnimationPropertyUtility;

// Lightingの編集値を登録する
void Engine::RegisterLightingAnimationProperties(AnimationPropertyRegistry& registry) {

	Register(registry, "DirectionalLight", "color", "DirectionalLight.color", AnimationValueType::Color4,
		HasComponent<DirectionalLightComponent>,
		GetMember<DirectionalLightComponent, Color4, &DirectionalLightComponent::color>,
		SetMember<DirectionalLightComponent, Color4, &DirectionalLightComponent::color>);
	Register(registry, "DirectionalLight", "direction", "DirectionalLight.direction", AnimationValueType::Vector3,
		HasComponent<DirectionalLightComponent>,
		GetMember<DirectionalLightComponent, Vector3, &DirectionalLightComponent::direction>,
		SetMember<DirectionalLightComponent, Vector3, &DirectionalLightComponent::direction>);
	Register(registry, "DirectionalLight", "intensity", "DirectionalLight.intensity", AnimationValueType::Float,
		HasComponent<DirectionalLightComponent>,
		GetMember<DirectionalLightComponent, float, &DirectionalLightComponent::intensity>,
		SetMember<DirectionalLightComponent, float, &DirectionalLightComponent::intensity>);

	Register(registry, "PointLight", "color", "PointLight.color", AnimationValueType::Color4, HasComponent<PointLightComponent>,
		GetMember<PointLightComponent, Color4, &PointLightComponent::color>,
		SetMember<PointLightComponent, Color4, &PointLightComponent::color>);
	Register(registry, "PointLight", "intensity", "PointLight.intensity", AnimationValueType::Float,
		HasComponent<PointLightComponent>, GetMember<PointLightComponent, float, &PointLightComponent::intensity>,
		SetMember<PointLightComponent, float, &PointLightComponent::intensity>);
	Register(registry, "PointLight", "radius", "PointLight.radius", AnimationValueType::Float,
		HasComponent<PointLightComponent>, GetMember<PointLightComponent, float, &PointLightComponent::radius>,
		SetMember<PointLightComponent, float, &PointLightComponent::radius>);
	Register(registry, "PointLight", "decay", "PointLight.decay", AnimationValueType::Float, HasComponent<PointLightComponent>,
		GetMember<PointLightComponent, float, &PointLightComponent::decay>,
		SetMember<PointLightComponent, float, &PointLightComponent::decay>);

	Register(registry, "RectLight", "color", "RectLight.color", AnimationValueType::Color4, HasComponent<RectLightComponent>,
		GetMember<RectLightComponent, Color4, &RectLightComponent::color>,
		SetMember<RectLightComponent, Color4, &RectLightComponent::color>);
	Register(registry, "RectLight", "intensity", "RectLight.intensity", AnimationValueType::Float,
		HasComponent<RectLightComponent>, GetMember<RectLightComponent, float, &RectLightComponent::intensity>,
		SetMember<RectLightComponent, float, &RectLightComponent::intensity>);
	Register(registry, "RectLight", "attenuationRadius", "RectLight.attenuationRadius", AnimationValueType::Float,
		HasComponent<RectLightComponent>, GetMember<RectLightComponent, float, &RectLightComponent::attenuationRadius>,
		SetMember<RectLightComponent, float, &RectLightComponent::attenuationRadius>);
	Register(registry, "RectLight", "sourceWidth", "RectLight.sourceWidth", AnimationValueType::Float,
		HasComponent<RectLightComponent>, GetMember<RectLightComponent, float, &RectLightComponent::sourceWidth>,
		SetMember<RectLightComponent, float, &RectLightComponent::sourceWidth>);
	Register(registry, "RectLight", "sourceHeight", "RectLight.sourceHeight", AnimationValueType::Float,
		HasComponent<RectLightComponent>, GetMember<RectLightComponent, float, &RectLightComponent::sourceHeight>,
		SetMember<RectLightComponent, float, &RectLightComponent::sourceHeight>);
	Register(registry, "RectLight", "decay", "RectLight.decay", AnimationValueType::Float, HasComponent<RectLightComponent>,
		GetMember<RectLightComponent, float, &RectLightComponent::decay>,
		SetMember<RectLightComponent, float, &RectLightComponent::decay>);
	Register(registry, "RectLight", "barnDoorAngle", "RectLight.barnDoorAngle", AnimationValueType::Float,
		HasComponent<RectLightComponent>, GetMember<RectLightComponent, float, &RectLightComponent::barnDoorAngle>,
		SetMember<RectLightComponent, float, &RectLightComponent::barnDoorAngle>);
	Register(registry, "RectLight", "barnDoorLength", "RectLight.barnDoorLength", AnimationValueType::Float,
		HasComponent<RectLightComponent>, GetMember<RectLightComponent, float, &RectLightComponent::barnDoorLength>,
		SetMember<RectLightComponent, float, &RectLightComponent::barnDoorLength>);

	Register(registry, "SpotLight", "color", "SpotLight.color", AnimationValueType::Color4, HasComponent<SpotLightComponent>,
		GetMember<SpotLightComponent, Color4, &SpotLightComponent::color>,
		SetMember<SpotLightComponent, Color4, &SpotLightComponent::color>);
	Register(registry, "SpotLight", "direction", "SpotLight.direction", AnimationValueType::Vector3,
		HasComponent<SpotLightComponent>, GetMember<SpotLightComponent, Vector3, &SpotLightComponent::direction>,
		SetMember<SpotLightComponent, Vector3, &SpotLightComponent::direction>);
	Register(registry, "SpotLight", "intensity", "SpotLight.intensity", AnimationValueType::Float,
		HasComponent<SpotLightComponent>, GetMember<SpotLightComponent, float, &SpotLightComponent::intensity>,
		SetMember<SpotLightComponent, float, &SpotLightComponent::intensity>);
	Register(registry, "SpotLight", "distance", "SpotLight.distance", AnimationValueType::Float,
		HasComponent<SpotLightComponent>, GetMember<SpotLightComponent, float, &SpotLightComponent::distance>,
		SetMember<SpotLightComponent, float, &SpotLightComponent::distance>);
	Register(registry, "SpotLight", "decay", "SpotLight.decay", AnimationValueType::Float, HasComponent<SpotLightComponent>,
		GetMember<SpotLightComponent, float, &SpotLightComponent::decay>,
		SetMember<SpotLightComponent, float, &SpotLightComponent::decay>);
	Register(registry, "SpotLight", "cosAngle", "SpotLight.cosAngle", AnimationValueType::Float,
		HasComponent<SpotLightComponent>, GetMember<SpotLightComponent, float, &SpotLightComponent::cosAngle>,
		SetMember<SpotLightComponent, float, &SpotLightComponent::cosAngle>);
	Register(registry, "SpotLight", "cosFalloffStart", "SpotLight.cosFalloffStart", AnimationValueType::Float,
		HasComponent<SpotLightComponent>, GetMember<SpotLightComponent, float, &SpotLightComponent::cosFalloffStart>,
		SetMember<SpotLightComponent, float, &SpotLightComponent::cosFalloffStart>);
}
