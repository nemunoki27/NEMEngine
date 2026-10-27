#include "ManagedCameraSettings.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/Rendering/Assets/RenderAssetLibrary.h>

//============================================================================
//	ManagedCameraSettings functions
//============================================================================
Engine::CameraCommon* Engine::ManagedCameraSettings::ResolveCamera(ECSWorld& world, Entity entity, int32_t type) {

	if (type == 17) {
		auto* camera = world.TryGetComponentForBinding<PerspectiveCameraComponent>(entity);
		return camera ? &camera->common : nullptr;
	}
	if (type == 16) {
		auto* camera = world.TryGetComponentForBinding<OrthographicCameraComponent>(entity);
		return camera ? &camera->common : nullptr;
	}
	return nullptr;
}

const Engine::RenderPassesAsset* Engine::ManagedCameraSettings::ResolvePasses(
	const CameraCommon& camera, RenderAssetLibrary& library) {

	if (camera.runtimeRenderPasses && camera.runtimeRenderPasses->guid == camera.renderPasses &&
		camera.runtimeRenderPassesSourceRevision == library.GetRenderPassesRevision(camera.renderPasses))
		return camera.runtimeRenderPasses.get();
	return library.LoadRenderPasses(camera.renderPasses);
}
