#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <string>

namespace Engine::EditorSceneDefaults {

	// 新規3Dカメラの描画設定を作る
	PerspectiveCameraComponent MakeCamera();
	// カメラと平行光源を持つ新規Sceneを作る
	nlohmann::json MakeScene(const std::string& name);
}
