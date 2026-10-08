#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>

// c++
#include <string>

namespace Engine {

	namespace SceneViewCameraDefaults {

		// 2Dカメラの既定値
		extern const Vector3 kDefault2DPosition;
		constexpr float kDefault2DZoom = 1.0f;
		constexpr float kMin2DZoom = 0.01f;
		constexpr float kMax2DZoom = 100.0f;
		constexpr float kDefault2DNearClip = 0.0f;
		constexpr float kDefault2DFarClip = 1000.0f;
		constexpr float kDefault2DPanSpeed = 1.0f;
		constexpr float kDefault2DZoomRate = 0.15f;
		// 3Dカメラの既定値
		extern const Vector3 kDefaultPosition;
		extern const Vector3 kDefaultRotation;
		constexpr float kDefaultFovY = 30.9397202f;
		constexpr float kDefaultNearClip = 0.1f;
		constexpr float kDefaultFarClip = 8000.0f;
		constexpr float kDefaultRotateSpeed = 0.005f;
		constexpr float kDefaultZoomRate = 0.4f;
		constexpr float kDefaultPanSpeed = 0.02f;
	}

	//============================================================================
	//	SceneViewCameraSettings struct
	//	シーンカメラの保存値と操作速度
	//============================================================================
	struct SceneViewCameraSettings {

		//========================================================================
		//	public Methods
		//========================================================================

		SceneViewCameraSettings();

		//--------- variables ----------------------------------------------------

		// カメラの位置と投影
		ManualRenderCameraState cameraState;
		// 2Dのズームと移動速度
		float zoomRate2D = SceneViewCameraDefaults::kDefault2DZoomRate;
		float panSpeed2D = SceneViewCameraDefaults::kDefault2DPanSpeed;
		// 3Dの回転と移動速度
		float rotateSpeed = SceneViewCameraDefaults::kDefaultRotateSpeed;
		float zoomRate = SceneViewCameraDefaults::kDefaultZoomRate;
		float panSpeed = SceneViewCameraDefaults::kDefaultPanSpeed;
	};

	namespace SceneViewCameraSettingsUtility {

		// カメラ計算に使える有限値か判定する
		bool IsFinite(const Vector3& value);
		// 設定ファイルから位置と操作速度を取り込む
		bool Load(const std::string& filePath, SceneViewCameraSettings& settings);
		// 無効値を補正して設定を保存する
		bool Save(const std::string& filePath, const SceneViewCameraSettings& source);
	}
}
