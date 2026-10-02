#pragma once

//============================================================================
//	include
//============================================================================
#include "RenderViewTypes.h"
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

namespace Engine::CameraViewSelection {

	struct ResolvedPerspectiveCameraOutput {

		ResolvedCameraView camera{};
		CameraCommon output{};
	};

	ResolvedCameraView ResolveBestOrthographicCamera( ECSWorld& world, uint32_t width, uint32_t height);

	ResolvedCameraView ResolveBestPerspectiveCamera( ECSWorld& world, uint32_t width, uint32_t height);

	// 有効な3Dカメラを出力順に確定する
	std::vector<ResolvedPerspectiveCameraOutput> ResolvePerspectiveCameraOutputs(
		ECSWorld& world, uint32_t width, uint32_t height);

	ResolvedCameraView ResolvePreferredOrthographicCamera(
		ECSWorld& world, UUID preferredCameraUUID, uint32_t width, uint32_t height);

	ResolvedCameraView ResolvePreferredPerspectiveCamera(
		ECSWorld& world, UUID preferredCameraUUID, uint32_t width, uint32_t height);
}
