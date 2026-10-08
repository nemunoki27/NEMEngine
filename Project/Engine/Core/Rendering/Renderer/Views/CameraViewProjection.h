#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderViewTypes.h"
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::CameraViewProjection {

	Matrix4x4 BuildManualWorld(const ManualRenderCameraTransform& transform);

	ResolvedCameraView BuildScreenCamera(uint32_t width, uint32_t height);

	void UpdateOrthographicCameraMatrices(const TransformComponent& transform,
		OrthographicCameraComponent& camera, uint32_t width, uint32_t height);

	void UpdatePerspectiveCameraMatrices(const TransformComponent& transform,
		PerspectiveCameraComponent& camera, uint32_t width, uint32_t height);

	ResolvedCameraView BuildFromOrthographicCamera( const Entity& entity, const TransformComponent& transform,
		OrthographicCameraComponent& camera);

	ResolvedCameraView BuildFromPerspectiveCamera(
		const Entity& entity, const TransformComponent& transform, PerspectiveCameraComponent& camera);

	ResolvedCameraView BuildManualOrthographic( const ManualRenderCameraState& state, uint32_t width, uint32_t height);

	ResolvedCameraView BuildManualPerspective( const ManualRenderCameraState& state, uint32_t width, uint32_t height);
}
