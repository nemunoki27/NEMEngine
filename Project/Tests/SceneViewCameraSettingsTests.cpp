#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Tools/Builtin/Camera/SceneViewCameraSettings.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

// c++
#include <cmath>
#include <limits>

bool NEMTests::TestSceneViewCameraSettings() {

	using namespace Engine;
	TestDirectory directory("SceneViewCameraSettings");
	const auto path = directory.GetPath() / "camera.json";
	SceneViewCameraSettings settings;
	settings.cameraState.transform2D.pos = Vector3(1.0f, 2.0f, 3.0f);
	settings.cameraState.transform3D.pos = Vector3(4.0f, 5.0f, 6.0f);
	settings.cameraState.transform3D.rotation = Vector3(7.0f, 8.0f, 9.0f);
	settings.cameraState.orthographicZoom = 2.0f;
	settings.cameraState.orthoNearClip = -10.0f;
	settings.cameraState.orthoFarClip = 20.0f;
	settings.cameraState.orthographicCullingMask = 2;
	settings.cameraState.perspectiveFovY = 70.0f;
	settings.cameraState.perspectiveNearClip = 0.5f;
	settings.cameraState.perspectiveFarClip = 100.0f;
	settings.cameraState.perspectiveCullingMask = 4;
	settings.zoomRate2D = 0.3f;
	settings.panSpeed2D = 2.0f;
	settings.rotateSpeed = 0.02f;
	settings.zoomRate = 0.8f;
	settings.panSpeed = 0.04f;
	if (!SceneViewCameraSettingsUtility::Save(path.string(), settings)) {
		return false;
	}
	SceneViewCameraSettings loaded;
	if (!SceneViewCameraSettingsUtility::Load(path.string(), loaded) ||
		loaded.cameraState.transform2D.pos != settings.cameraState.transform2D.pos ||
		loaded.cameraState.transform3D.pos != settings.cameraState.transform3D.pos ||
		loaded.cameraState.transform3D.rotation != settings.cameraState.transform3D.rotation ||
		loaded.cameraState.orthographicZoom != 2.0f || loaded.cameraState.orthoNearClip != -10.0f ||
		loaded.cameraState.orthoFarClip != 20.0f || loaded.cameraState.orthographicCullingMask != 2 ||
		loaded.cameraState.perspectiveFovY != 70.0f || loaded.cameraState.perspectiveNearClip != 0.5f ||
		loaded.cameraState.perspectiveFarClip != 100.0f || loaded.cameraState.perspectiveCullingMask != 4 ||
		loaded.zoomRate2D != 0.3f || loaded.panSpeed2D != 2.0f || loaded.rotateSpeed != 0.02f || loaded.zoomRate != 0.8f ||
		loaded.panSpeed != 0.04f) {
		return false;
	}

	// 保存できないときは既存ファイルと入力を保つ
	nlohmann::json original;
	if (!JsonFile::TryLoad(path, original)) {
		return false;
	}
	{
		TestFileReadLock lock(path);
		if (SceneViewCameraSettingsUtility::Save(path.string(), SceneViewCameraSettings{})) {
			return false;
		}
	}
	nlohmann::json retained;
	if (!JsonFile::TryLoad(path, retained) || retained != original) {
		return false;
	}

	// 欠損ファイルと不正な型では現在値を引き継ぐ
	if (SceneViewCameraSettingsUtility::Load((directory.GetPath() / "missing.json").string(), loaded) ||
		loaded.cameraState.perspectiveFovY != 70.0f) {
		return false;
	}
	const nlohmann::json invalid = {{"perspectiveFovY", "invalid"}, {"perspectiveCullingMask", 4294967295ULL},
		{"rotateSpeed", -1.0f}, {"orthographicZoom", 1000.0f}};
	if (!JsonFile::Save(path, invalid) || !SceneViewCameraSettingsUtility::Load(path.string(), loaded) ||
		loaded.cameraState.perspectiveFovY != 70.0f || loaded.cameraState.perspectiveCullingMask != 4 ||
		loaded.rotateSpeed != 0.0f || loaded.cameraState.orthographicZoom != SceneViewCameraDefaults::kMax2DZoom) {
		return false;
	}

	// 非有限値は保存用の値だけを補正する
	settings.cameraState.perspectiveFovY = std::numeric_limits<float>::quiet_NaN();
	settings.panSpeed = -1.0f;
	if (!SceneViewCameraSettingsUtility::Save(path.string(), settings) || !std::isnan(settings.cameraState.perspectiveFovY) ||
		settings.panSpeed != -1.0f || !SceneViewCameraSettingsUtility::Load(path.string(), loaded)) {
		return false;
	}
	return loaded.cameraState.perspectiveFovY == SceneViewCameraDefaults::kDefaultFovY &&
		   loaded.panSpeed == SceneViewCameraDefaults::kDefaultPanSpeed;
}
