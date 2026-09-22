#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderViewTypes.h"
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::CameraViewSelection {

	ResolvedCameraView ResolveBestOrthographicCamera( ECSWorld& world, uint32_t width, uint32_t height);

	ResolvedCameraView ResolveBestPerspectiveCamera( ECSWorld& world, uint32_t width, uint32_t height);

	ResolvedCameraView ResolvePreferredOrthographicCamera(
		ECSWorld& world, UUID preferredCameraUUID, uint32_t width, uint32_t height);

	ResolvedCameraView ResolvePreferredPerspectiveCamera(
		ECSWorld& world, UUID preferredCameraUUID, uint32_t width, uint32_t height);
}
