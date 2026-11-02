#include "InspectorCommonBindings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Animation/AnimationClipPlayerComponent.h>
#include <Engine/Core/World/Components/Animation/SimpleAnimationComponent.h>
#include <Engine/Core/World/Components/Animation/SkinnedAnimationComponent.h>
#include <Engine/Core/World/Components/Audio/AudioListenerComponent.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Core/World/Components/Lighting/DirectionalLightComponent.h>
#include <Engine/Core/World/Components/Lighting/PointLightComponent.h>
#include <Engine/Core/World/Components/Lighting/RectLightComponent.h>
#include <Engine/Core/World/Components/Lighting/SpotLightComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/ParticleFieldComponents.h>
#include <Engine/Core/World/Components/Physics/PhysicsJointComponent.h>
#include <Engine/Core/World/Components/Rendering/DecalProjectorComponent.h>
#include <Engine/Core/World/Components/Rendering/FlipbookAnimationComponent.h>
#include <Engine/Core/World/Components/Rendering/InvertedHullOutlineComponent.h>
#include <Engine/Core/World/Components/Rendering/LineRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticlePaintReceiverComponent.h>
#include <Engine/Core/World/Components/Rendering/ParticleSystemComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/ScreenSpaceOutlineComponent.h>
#include <Engine/Core/World/Components/Rendering/SkyboxRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/SpriteRendererComponent.h>
#include <Engine/Core/World/Components/Rendering/TextRendererComponent.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Components/UI/UIImageButtonComponent.h>
#include <Engine/Core/World/Components/UI/UIProgressComponent.h>
#include <Engine/Core/World/Components/UI/UITextButtonComponent.h>

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(AudioSourceComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(AudioListenerComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(PerspectiveCameraComponent& value) {

	return { .enabled = &value.common.enabled, .cameraLayer = &value.common.cullingMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(OrthographicCameraComponent& value) {

	return { .enabled = &value.common.enabled, .cameraLayer = &value.common.cullingMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(CameraControllerComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(MeshRendererComponent& value) {

	return { .visible = &value.visible, .displayLayer = &value.renderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(SpriteRendererComponent& value) {

	return { .visible = &value.visible, .displayLayer = &value.renderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(TextRendererComponent& value) {

	return { .visible = &value.visible, .displayLayer = &value.renderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(SkyboxRendererComponent& value) {

	return { .visible = &value.visible, .displayLayer = &value.renderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(DecalProjectorComponent& value) {

	return { .enabled = &value.enabled, .displayLayer = &value.renderingLayerMask, .receiverLayer = &value.receiverRenderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(LineRendererComponent& value) {

	return { .visible = &value.visible, .displayLayer = &value.renderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(PrimitiveRendererComponent& value) {

	return { .visible = &value.visible, .displayLayer = &value.renderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(ParticleSystemComponent& value) {

	return { .enabled = &value.enabled, .visible = &value.visible, .displayLayer = &value.renderingLayerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(ParticlePaintReceiverComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(FlipbookAnimationComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(InvertedHullOutlineComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(ScreenSpaceOutlineComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(CollisionComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(FixedJointComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(HingeJointComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(CanvasComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(UIImageButtonComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(UITextButtonComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(UIProgressComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(SimpleAnimationComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(SkinnedAnimationComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(AnimationClipPlayerComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(DirectionalLightComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(PointLightComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(RectLightComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(SpotLightComponent& value) {

	return { .enabled = &value.enabled };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(ForceFieldComponent& value) {

	return { .enabled = &value.enabled, .particleGroup = &value.layerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(VectorFieldComponent& value) {

	return { .enabled = &value.enabled, .particleGroup = &value.layerMask };
}

Engine::InspectorCommonFields Engine::BindInspectorCommonFields(HeightmapColliderComponent& value) {

	return { .enabled = &value.enabled, .particleGroup = &value.layerMask };
}

