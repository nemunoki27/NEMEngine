#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/MathLib/Matrix4x4.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	CameraComponent struct
	//============================================================================

	// カメラ共通の設定
	struct CameraCommon {

		// アスペクト比
		float aspectRatio = 0.0f;
		// 優先度
		int32_t priority = 0;
		// 描画対象レイヤーマスク
		int32_t cullingMask = -1;
		// 有効/無効
		bool enabled = true;
		// MainCameraとして扱うか
		bool isMain = true;

		// エディターに表示するフラスタムのサイズ
		float editorFrustumScale = 0.002f;

		// ランタイム行列
		Matrix4x4 viewMatrix = Matrix4x4::Identity();
		Matrix4x4 projectionMatrix = Matrix4x4::Identity();
		Matrix4x4 viewProjectionMatrix = Matrix4x4::Identity();
	};

	// 2Dカメラ
	struct OrthographicCameraComponent {

		// クリップ範囲
		float nearClip = 0.0f;
		float farClip = 1000.0f;

		CameraCommon common;
	};
	// 3Dカメラ
	struct PerspectiveCameraComponent {

		// 画角
		float fovY = 60.0f;
		// クリップ範囲
		float nearClip = 0.01f;
		float farClip = 4000.0f;

		CameraCommon common;
	};

	// json変換
	void from_json(const nlohmann::json& in, OrthographicCameraComponent& component);
	void to_json(nlohmann::json& out, const OrthographicCameraComponent& component);
	void from_json(const nlohmann::json& in, PerspectiveCameraComponent& component);
	void to_json(nlohmann::json& out, const PerspectiveCameraComponent& component);

	ENGINE_REGISTER_COMPONENT(OrthographicCameraComponent, "OrthographicCamera");
	ENGINE_REGISTER_COMPONENT(PerspectiveCameraComponent, "PerspectiveCamera");
} // Engine