#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::CameraControllerUpdate {

	float MakeLerpRate(float speed, float deltaTime);

	Vector3 ClampVector(const Vector3& value, const Vector3& minValue, const Vector3& maxValue);

	Matrix4x4 ResolveWorldMatrix(ECSWorld& world, const Entity& entity);

	Vector3 GetWorldPosition(ECSWorld& world, const Entity& entity);

	Vector3 WorldToParentLocal(ECSWorld& world, const Entity& entity, const Vector3& worldPosition);

	Quaternion UpdateOrbitRotation(const Quaternion& current, CameraFollowSettings& follow, float deltaTime);

	Vector3 ApplyFollow(ECSWorld& world, const Entity& entity, TransformComponent& transform,
		CameraFollowSettings& follow, float deltaTime, bool allowInput);

	bool ApplyLookAt(ECSWorld& world, const Entity& entity, TransformComponent& transform, const CameraLookAtSettings& lookAt, float deltaTime);
}
