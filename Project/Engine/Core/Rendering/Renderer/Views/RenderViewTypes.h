#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/Components/Camera/CameraComponent.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Vector3.h>
#include <string>

namespace Engine {

	//============================================================================
	//	RenderViewKind structures
	//============================================================================
	// 描画ビューの種類
	enum class RenderViewKind {

		Game,
		Scene,
	};

	// どの種類のカメラを使う描画か
	enum class RenderCameraDomain :
		uint8_t {

		Orthographic,
		Perspective,
		Screen,
	};

	// 確定したカメラの投影方式
	enum class ResolvedProjectionMode :
		uint8_t {

		Perspective,
		Orthographic,
	};

	// 2D/3Dカメラ共通トランスフォーム
	struct ManualRenderCameraTransform {

		Vector3 pos = Vector3::AnyInit(0.0f);
		Vector3 rotation = Vector3::AnyInit(0.0f);
	};

	// ECS外の手動カメラ状態
	struct ManualRenderCameraState {

		// カメラの位置と回転
		ManualRenderCameraTransform transform2D{};
		ManualRenderCameraTransform transform3D{};

		// 2D
		bool enableOrthographic = true;
		float orthoNearClip = 0.0f;
		float orthoFarClip = 1000.0f;
		float orthographicZoom = 1.0f;
		int32_t orthographicCullingMask = -1;

		// 3D
		bool enablePerspective = true;
		float perspectiveFovY = 60.0f;
		float perspectiveNearClip = 0.01f;
		float perspectiveFarClip = 4000.0f;
		int32_t perspectiveCullingMask = -1;
	};

	// 投影方式ごとのカメラ行列
	struct ViewProjectionMatrices {

		Matrix4x4 viewMatrix = Matrix4x4::Identity();
		Matrix4x4 projectionMatrix = Matrix4x4::Identity();
		Matrix4x4 viewProjectionMatrix = Matrix4x4::Identity();
		Matrix4x4 inverseViewMatrix = Matrix4x4::Identity();
		Matrix4x4 inverseProjectionMatrix = Matrix4x4::Identity();
	};

	// 描画時に使用する確定済みカメラ情報
	struct ResolvedCameraView {

		// ビュー情報が有効か
		bool valid = false;
		// 投影方式
		ResolvedProjectionMode projectionMode = ResolvedProjectionMode::Perspective;

		// 描画ビュー行列
		ViewProjectionMatrices matrices{};
		// カメラの位置
		Vector3 cameraPos = Vector3::AnyInit(0.0f);
		Vector3 forward = Vector3::AnyInit(0.0f);

		// クリップ範囲
		float nearClip = 0.1f;
		float farClip = 1000.0f;
		// 描画対象レイヤーマスク
		uint32_t cullingMask = 0xFFFFFFFFu;
		bool postProcessEnabled = true;
		ColorPipelineSettings colorPipeline{};
		AssetID renderPasses{};

		// 描画にしようするカメラを持つエンティティ
		Entity sourceCamera = Entity::Null();
		// ECS外の手動カメラ状態を使用しているか
		bool usesManualCamera = false;
	};

	struct ResolvedRenderView {

		// 描画ビューの種類
		RenderViewKind kind = RenderViewKind::Game;
		// 描画ビューの情報が有効か
		bool valid = false;

		// World切替ごとに履歴の所有を区別する
		uint64_t historyWorldRevision = 0;

		// 描画範囲
		uint32_t width = 0;
		uint32_t height = 0;
		float aspectRatio = 1.0f;
		// 出力先内の描画範囲
		uint32_t outputX = 0;
		uint32_t outputY = 0;
		uint32_t outputWidth = 0;
		uint32_t outputHeight = 0;
		float normalizedOutputX = 0.0f;
		float normalizedOutputY = 0.0f;
		float normalizedOutputWidth = 1.0f;
		float normalizedOutputHeight = 1.0f;
		// 空なら既定のViewサーフェスへ出力する
		AssetID targetTexture{};

		ResolvedCameraView orthographic{};
		ResolvedCameraView perspective{};
		ResolvedCameraView screen{};

		// Cameraと出力先から履歴の識別子を作る
		std::string GetHistoryKey() const;
		// 出力Cameraと投影Cameraの描画対象を合わせる
		uint32_t GetCullingMask(RenderCameraDomain domain) const;
		// 指定した投影方式のカメラビューを返す
		const ResolvedCameraView* FindCamera(RenderCameraDomain domain) const;
	};
} // Engine
