#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	CameraControlMode enum
	//	カメラ制御の適用方法
	//============================================================================
	enum class CameraControlMode :
		int32_t {

		None,
		Follow,       // 追従
		LookAt,       // 注視
		FollowLookAt, // 追従と注視
	};

	//============================================================================
	//	CameraFollowSettings structure
	//	ターゲット追従に必要な設定
	//============================================================================
	struct CameraFollowSettings {

		// 追従処理を有効にするか
		bool enabled = true;
		// 追従対象EntityのUUID
		UUID target{};
		// ターゲットからのワールドオフセット
		Vector3 offset = Vector3(0.0f, 3.0f, -8.0f);
		// 追従する軸は0で現在値を維持し、1で追従する
		Vector3 axisMask = Vector3::AnyInit(1.0f);
		// 0以下の場合は補間せず即座に追従する
		float posLerpSpeed = 8.0f;
	};

	//============================================================================
	//	CameraLookAtSettings structure
	//	ターゲット注視に必要な設定
	//============================================================================
	struct CameraLookAtSettings {

		// 注視処理を有効にするか
		bool enabled = false;
		// 注視対象EntityのUUID
		UUID target{};
		// 注視点に加えるワールドオフセット
		Vector3 offset = Vector3::AnyInit(0.0f);
		// 0以下の場合は補間せず即座に回転する
		float rotationLerpSpeed = 12.0f;
		// Rollを0度に固定するか
		bool lockRoll = true;
	};

	//============================================================================
	//	CameraFollowLookAtSettings structure
	//	追従と注視を同時に行うモードの設定
	//============================================================================
	struct CameraFollowLookAtSettings {

		CameraFollowSettings follow{};
		CameraLookAtSettings lookAt{};
	};

	//============================================================================
	//	CameraControllerComponent structure
	//	カメラの追従、注視、揺れを管理するコンポーネント
	//============================================================================
	struct CameraControllerComponent {

		// コントローラー全体の有効状態
		bool enabled = true;
		// 適用する制御方法
		CameraControlMode mode = CameraControlMode::Follow;
		// Editモード中もプレビューとして制御を動かすか
		bool editorPreview = false;

		// ターゲット追従設定
		CameraFollowSettings follow{};
		// ターゲット注視設定
		CameraLookAtSettings lookAt{};
		// 追従と注視を同時に行うモードの設定
		CameraFollowLookAtSettings followLookAt{};
	};

	// jsonからコンポーネントへ変換する
	void from_json(const nlohmann::json& in, CameraControllerComponent& component);
	// コンポーネントからjsonへ変換する
	void to_json(nlohmann::json& out, const CameraControllerComponent& component);

	ENGINE_REGISTER_COMPONENT(CameraControllerComponent, "CameraController");
} // Engine
