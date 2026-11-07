#pragma once

//============================================================================
//	include
//============================================================================
#include "InspectorPresentation.h"

namespace Engine {

	template <typename T>
	InspectorCommonFields BindInspectorCommonFields(T&) { return {}; }

	struct TimeScaleComponent;
	struct TimerComponent;
	struct AudioSourceComponent;
	struct AudioListenerComponent;
	struct PerspectiveCameraComponent;
	struct OrthographicCameraComponent;
	struct CameraControllerComponent;
	struct MeshRendererComponent;
	struct SpriteRendererComponent;
	struct TextRendererComponent;
	struct SkyAtmosphereComponent;
	struct ExponentialHeightFogComponent;
	struct SkyboxRendererComponent;
	struct DecalProjectorComponent;
	struct LineRendererComponent;
	struct PrimitiveRendererComponent;
	struct ParticleSystemComponent;
	struct ParticlePaintReceiverComponent;
	struct FlipbookAnimationComponent;
	struct InvertedHullOutlineComponent;
	struct ScreenSpaceOutlineComponent;
	struct CollisionComponent;
	struct FixedJointComponent;
	struct HingeJointComponent;
	struct CanvasComponent;
	struct UIImageButtonComponent;
	struct UITextButtonComponent;
	struct UIProgressComponent;
	struct SimpleAnimationComponent;
	struct SkinnedAnimationComponent;
	struct AnimationClipPlayerComponent;
	struct DirectionalLightComponent;
	struct PointLightComponent;
	struct RectLightComponent;
	struct SpotLightComponent;
	struct ForceFieldComponent;
	struct VectorFieldComponent;
	struct HeightmapColliderComponent;

	InspectorCommonFields BindInspectorCommonFields(TimeScaleComponent& value);
	InspectorCommonFields BindInspectorCommonFields(TimerComponent& value);
	InspectorCommonFields BindInspectorCommonFields(AudioSourceComponent& value);
	InspectorCommonFields BindInspectorCommonFields(AudioListenerComponent& value);
	InspectorCommonFields BindInspectorCommonFields(PerspectiveCameraComponent& value);
	InspectorCommonFields BindInspectorCommonFields(OrthographicCameraComponent& value);
	InspectorCommonFields BindInspectorCommonFields(CameraControllerComponent& value);
	InspectorCommonFields BindInspectorCommonFields(MeshRendererComponent& value);
	InspectorCommonFields BindInspectorCommonFields(SpriteRendererComponent& value);
	InspectorCommonFields BindInspectorCommonFields(TextRendererComponent& value);
	InspectorCommonFields BindInspectorCommonFields(SkyAtmosphereComponent& value);
	InspectorCommonFields BindInspectorCommonFields(ExponentialHeightFogComponent& value);
	InspectorCommonFields BindInspectorCommonFields(SkyboxRendererComponent& value);
	InspectorCommonFields BindInspectorCommonFields(DecalProjectorComponent& value);
	InspectorCommonFields BindInspectorCommonFields(LineRendererComponent& value);
	InspectorCommonFields BindInspectorCommonFields(PrimitiveRendererComponent& value);
	InspectorCommonFields BindInspectorCommonFields(ParticleSystemComponent& value);
	InspectorCommonFields BindInspectorCommonFields(ParticlePaintReceiverComponent& value);
	InspectorCommonFields BindInspectorCommonFields(FlipbookAnimationComponent& value);
	InspectorCommonFields BindInspectorCommonFields(InvertedHullOutlineComponent& value);
	InspectorCommonFields BindInspectorCommonFields(ScreenSpaceOutlineComponent& value);
	InspectorCommonFields BindInspectorCommonFields(CollisionComponent& value);
	InspectorCommonFields BindInspectorCommonFields(FixedJointComponent& value);
	InspectorCommonFields BindInspectorCommonFields(HingeJointComponent& value);
	InspectorCommonFields BindInspectorCommonFields(CanvasComponent& value);
	InspectorCommonFields BindInspectorCommonFields(UIImageButtonComponent& value);
	InspectorCommonFields BindInspectorCommonFields(UITextButtonComponent& value);
	InspectorCommonFields BindInspectorCommonFields(UIProgressComponent& value);
	InspectorCommonFields BindInspectorCommonFields(SimpleAnimationComponent& value);
	InspectorCommonFields BindInspectorCommonFields(SkinnedAnimationComponent& value);
	InspectorCommonFields BindInspectorCommonFields(AnimationClipPlayerComponent& value);
	InspectorCommonFields BindInspectorCommonFields(DirectionalLightComponent& value);
	InspectorCommonFields BindInspectorCommonFields(PointLightComponent& value);
	InspectorCommonFields BindInspectorCommonFields(RectLightComponent& value);
	InspectorCommonFields BindInspectorCommonFields(SpotLightComponent& value);
	InspectorCommonFields BindInspectorCommonFields(ForceFieldComponent& value);
	InspectorCommonFields BindInspectorCommonFields(VectorFieldComponent& value);
	InspectorCommonFields BindInspectorCommonFields(HeightmapColliderComponent& value);
}
