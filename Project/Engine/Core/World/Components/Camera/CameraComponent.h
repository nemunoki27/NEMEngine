#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfile.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	CameraProjectionMode enum
	//	3Dカメラの投影方式
	//============================================================================
	enum class CameraProjectionMode :
		int32_t {

		Perspective,
		Orthographic,
	};

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
		// 出力先内の正規化描画範囲
		float viewportX = 0.0f;
		float viewportY = 0.0f;
		float viewportWidth = 1.0f;
		float viewportHeight = 1.0f;
		// 空ならGame Viewへ出力する
		AssetID targetTexture{};
		// Camera固有の露出と色補正
		bool postProcessEnabled = true;
		ColorPipelineSettings colorPipeline{};
		// Camera固有の描画拡張
		AssetID renderPasses{};

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

		// Projectの間接光をこのCameraで使用するか
		bool useGlobalIllumination = true;

		// 投影方式
		CameraProjectionMode projectionMode = CameraProjectionMode::Perspective;
		// 画角
		float fovY = 60.0f;
		// 平行投影の縦半径
		float orthographicSize = 5.0f;
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

} // Engine
