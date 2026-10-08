#include "BuiltinAnimationPropertyGroups.h"

//============================================================================
//	include
//============================================================================
#include "BuiltinAnimationPropertyUtility.h"
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Rendering/UVTransformComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>

using namespace Engine::AnimationPropertyUtility;

// Runtimeの編集値を登録する
void Engine::RegisterRuntimeAnimationProperties(AnimationPropertyRegistry& registry) {

	Register(registry, "AudioSource", "volume", "AudioSource.volume", AnimationValueType::Float,
		HasComponent<AudioSourceComponent>, GetMember<AudioSourceComponent, float, &AudioSourceComponent::volume>,
		SetMember<AudioSourceComponent, float, &AudioSourceComponent::volume>);
	Register(registry, "UVTransform", "pos", "UVTransform.pos", AnimationValueType::Vector2, HasComponent<UVTransformComponent>,
		GetMember<UVTransformComponent, Vector2, &UVTransformComponent::pos>,
		SetMember<UVTransformComponent, Vector2, &UVTransformComponent::pos>);
	Register(registry, "UVTransform", "rotation", "UVTransform.rotation", AnimationValueType::Float,
		HasComponent<UVTransformComponent>, GetMember<UVTransformComponent, float, &UVTransformComponent::rotation>,
		SetMember<UVTransformComponent, float, &UVTransformComponent::rotation>);
	Register(registry, "UVTransform", "scale", "UVTransform.scale", AnimationValueType::Vector2,
		HasComponent<UVTransformComponent>, GetMember<UVTransformComponent, Vector2, &UVTransformComponent::scale>,
		SetMember<UVTransformComponent, Vector2, &UVTransformComponent::scale>);
	Register(registry, "SkinnedAnimation", "playbackSpeed", "SkinnedAnimation.playbackSpeed", AnimationValueType::Float,
		HasComponent<SkinnedAnimationComponent>,
		GetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::playbackSpeed>,
		SetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::playbackSpeed>);
	Register(registry, "SkinnedAnimation", "transitionDuration", "SkinnedAnimation.transitionDuration",
		AnimationValueType::Float, HasComponent<SkinnedAnimationComponent>,
		GetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::transitionDuration>,
		SetMember<SkinnedAnimationComponent, float, &SkinnedAnimationComponent::transitionDuration>);
}
