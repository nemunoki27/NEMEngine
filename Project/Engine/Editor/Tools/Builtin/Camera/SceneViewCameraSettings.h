#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Renderer/Views/RenderViewTypes.h>
#include "SceneViewCameraNavigation.h"

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
		constexpr float kDefaultMoveSpeed = 10.0f;
		constexpr float kMinMoveSpeed = 0.01f;
		constexpr float kMaxMoveSpeed = 10000.0f;
		constexpr float kDefaultOrbitDistance = 20.0f;
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
		// 他方の次元を保って設定を初期化する
		void Reset2D();
		void Reset3D();

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
		float moveSpeed = SceneViewCameraDefaults::kDefaultMoveSpeed;
		// 3Dの操作方式とオリジナルの割当
		SceneViewCameraControlStyle controlStyle = SceneViewCameraControlStyle::Unity;
		SceneViewCameraControls customControls{
			SceneViewCameraControlSource::Unity, SceneViewCameraControlSource::Unity, SceneViewCameraControlSource::Unity,
			SceneViewCameraControlSource::Unity, SceneViewCameraControlSource::Unity};
	};

	namespace SceneViewCameraSettingsUtility {

		// カメラ計算に使える有限値か判定する
		bool IsFinite(const Vector3& value);
		// 不正な方式と競合した割当を既定値へ戻す
		void ValidateControls(SceneViewCameraSettings& settings);
		// カメラ計算に使えない値を補正する
		void Sanitize(SceneViewCameraSettings& settings);
		// 設定ファイルから位置と操作速度を取り込む
		bool Load(const std::string& filePath, SceneViewCameraSettings& settings);
		// 無効値を補正して設定を保存する
		bool Save(const std::string& filePath, const SceneViewCameraSettings& source);
	}
}
