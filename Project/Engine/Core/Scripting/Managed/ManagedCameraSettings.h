#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderPassesAsset.h>

namespace Engine {

	class RenderAssetLibrary;
	class ECSWorld;

	namespace ManagedCameraSettings {

		// Cameraの種類と現在の格納先を解決する
		CameraCommon* ResolveCamera(ECSWorld& world, Entity entity, int32_t type);
		// Assetの世代と一致するCamera固有の設定を取得する
		const RenderPassesAsset* ResolvePasses(const CameraCommon& camera, RenderAssetLibrary& library);
	}
}
