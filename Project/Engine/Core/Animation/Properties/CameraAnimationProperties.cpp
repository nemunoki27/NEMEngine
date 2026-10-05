#include "BuiltinAnimationPropertyGroups.h"

//============================================================================
//	include
//============================================================================
#include "BuiltinAnimationPropertyUtility.h"
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>

using namespace Engine::AnimationPropertyUtility;

// Cameraの編集値を登録する
void Engine::RegisterCameraAnimationProperties(AnimationPropertyRegistry& registry) {

	Register(registry, "OrthographicCamera", "nearClip", "OrthographicCamera.nearClip", AnimationValueType::Float,
		HasComponent<OrthographicCameraComponent>,
		GetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::nearClip>,
		SetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::nearClip>);
	Register(registry, "OrthographicCamera", "farClip", "OrthographicCamera.farClip", AnimationValueType::Float,
		HasComponent<OrthographicCameraComponent>,
		GetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::farClip>,
		SetMember<OrthographicCameraComponent, float, &OrthographicCameraComponent::farClip>);
	Register(registry, "OrthographicCamera", "common.aspectRatio", "OrthographicCamera.aspectRatio", AnimationValueType::Float,
		HasComponent<OrthographicCameraComponent>,
		GetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common,
			&CameraCommon::aspectRatio>,
		SetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common,
			&CameraCommon::aspectRatio>);
	Register(registry, "OrthographicCamera", "common.editorFrustumScale", "OrthographicCamera.editorFrustumScale",
		AnimationValueType::Float, HasComponent<OrthographicCameraComponent>,
		GetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common,
			&CameraCommon::editorFrustumScale>,
		SetChildMember<OrthographicCameraComponent, CameraCommon, float, &OrthographicCameraComponent::common,
			&CameraCommon::editorFrustumScale>);

	Register(registry, "PerspectiveCamera", "fovY", "PerspectiveCamera.fovY", AnimationValueType::Float,
		HasComponent<PerspectiveCameraComponent>,
		GetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::fovY>,
		SetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::fovY>);
	Register(registry, "PerspectiveCamera", "nearClip", "PerspectiveCamera.nearClip", AnimationValueType::Float,
		HasComponent<PerspectiveCameraComponent>,
		GetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::nearClip>,
		SetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::nearClip>);
	Register(registry, "PerspectiveCamera", "farClip", "PerspectiveCamera.farClip", AnimationValueType::Float,
		HasComponent<PerspectiveCameraComponent>,
		GetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::farClip>,
		SetMember<PerspectiveCameraComponent, float, &PerspectiveCameraComponent::farClip>);
	Register(registry, "PerspectiveCamera", "common.aspectRatio", "PerspectiveCamera.aspectRatio", AnimationValueType::Float,
		HasComponent<PerspectiveCameraComponent>,
		GetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common,
			&CameraCommon::aspectRatio>,
		SetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common,
			&CameraCommon::aspectRatio>);
	Register(registry, "PerspectiveCamera", "common.editorFrustumScale", "PerspectiveCamera.editorFrustumScale",
		AnimationValueType::Float, HasComponent<PerspectiveCameraComponent>,
		GetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common,
			&CameraCommon::editorFrustumScale>,
		SetChildMember<PerspectiveCameraComponent, CameraCommon, float, &PerspectiveCameraComponent::common,
			&CameraCommon::editorFrustumScale>);
}

// CameraControllerの編集値を登録する
void Engine::RegisterCameraControllerAnimationProperties(AnimationPropertyRegistry& registry) {

	Register(registry, "CameraController", "follow.offset", "CameraController.follow.offset", AnimationValueType::Vector3,
		HasComponent<CameraControllerComponent>,
		GetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow,
			&CameraFollowSettings::offset>,
		SetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow,
			&CameraFollowSettings::offset>);
	Register(registry, "CameraController", "follow.axisMask", "CameraController.follow.axisMask", AnimationValueType::Vector3,
		HasComponent<CameraControllerComponent>,
		GetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow,
			&CameraFollowSettings::axisMask>,
		SetChildMember<CameraControllerComponent, CameraFollowSettings, Vector3, &CameraControllerComponent::follow,
			&CameraFollowSettings::axisMask>);
	Register(registry, "CameraController", "follow.posLerpSpeed", "CameraController.follow.posLerpSpeed",
		AnimationValueType::Float, HasComponent<CameraControllerComponent>,
		GetChildMember<CameraControllerComponent, CameraFollowSettings, float, &CameraControllerComponent::follow,
			&CameraFollowSettings::posLerpSpeed>,
		SetChildMember<CameraControllerComponent, CameraFollowSettings, float, &CameraControllerComponent::follow,
			&CameraFollowSettings::posLerpSpeed>);
	Register(registry, "CameraController", "lookAt.offset", "CameraController.lookAt.offset", AnimationValueType::Vector3,
		HasComponent<CameraControllerComponent>,
		GetChildMember<CameraControllerComponent, CameraLookAtSettings, Vector3, &CameraControllerComponent::lookAt,
			&CameraLookAtSettings::offset>,
		SetChildMember<CameraControllerComponent, CameraLookAtSettings, Vector3, &CameraControllerComponent::lookAt,
			&CameraLookAtSettings::offset>);
	Register(registry, "CameraController", "lookAt.rotationLerpSpeed", "CameraController.lookAt.rotationLerpSpeed",
		AnimationValueType::Float, HasComponent<CameraControllerComponent>,
		GetChildMember<CameraControllerComponent, CameraLookAtSettings, float, &CameraControllerComponent::lookAt,
			&CameraLookAtSettings::rotationLerpSpeed>,
		SetChildMember<CameraControllerComponent, CameraLookAtSettings, float, &CameraControllerComponent::lookAt,
			&CameraLookAtSettings::rotationLerpSpeed>);
}
