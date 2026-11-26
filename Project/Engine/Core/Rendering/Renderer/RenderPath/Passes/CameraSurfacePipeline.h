#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Core/SurfaceLightingSettings.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>

namespace Engine {

	namespace CameraSurfacePassIDs {

		inline constexpr UUID ReflectionTrace{0x52545245464c4543ull};
		inline constexpr UUID AmbientOcclusionTrace{0x414f545241434500ull};
		inline constexpr UUID AmbientOcclusionFilter{0x414f46494c544552ull};
	}

	// Cameraの固定反射パスを作成する
	RenderFeatureProfileAsset BuildCameraReflectionProfile(const SurfaceLightingSettings& settings);
	// Cameraの固定遮蔽パスを作成する
	RenderFeatureProfileAsset BuildCameraAmbientOcclusionProfile(const SurfaceLightingSettings& settings, bool opaqueGeometryAllowed = false, uint32_t cullingMask = UINT32_MAX);
}
