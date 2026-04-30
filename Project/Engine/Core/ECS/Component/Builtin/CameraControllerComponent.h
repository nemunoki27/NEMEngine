#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/UUID/UUID.h>
#include <Engine/MathLib/Vector3.h>

// c++
#include <string>

namespace Engine {

	//============================================================================
	//	CameraControlMode enum
	//	カメラ制御の適用方法
	//============================================================================
	enum class CameraControlMode : int32_t {

		None,
		Follow,
		LookAt,
		FollowLookAt,
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
		// 追従する軸。0で現在値を維持し、1で追従する
		Vector3 axisMask = Vector3::AnyInit(1.0f);
		// 0以下の場合は補間せず即座に追従する
		float positionLerpSpeed = 8.0f;

		// ワールド座標の移動範囲を制限するか
		bool useBounds = false;
		// 移動範囲の最小値
		Vector3 boundsMin = Vector3(-100.0f, -100.0f, -100.0f);
		// 移動範囲の最大値
		Vector3 boundsMax = Vector3(100.0f, 100.0f, 100.0f);
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
	//	CameraShakeSettings structure
	//	カメラ揺れに必要な設定
	//============================================================================
	struct CameraShakeSettings {

		// 揺れ処理を有効にするか
		bool enabled = true;
		// 揺れの強さ
		float amplitude = 0.2f;
		// 揺れ時間
		float duration = 0.3f;
		// 1秒あたりの揺れ周期
		float frequency = 18.0f;
		// 終端へ向けて減衰させる強さ
		float damping = 1.0f;
		// 揺れを適用する軸
		Vector3 axisMask = Vector3(1.0f, 1.0f, 0.0f);

		// 実行中の揺れ時間
		float runtimeTime = 0.0f;
		// 実行中の揺れ全体時間
		float runtimeDuration = 0.0f;
		// 実行中の揺れ強度
		float runtimeAmplitude = 0.0f;
		// 前フレームに適用した揺れオフセット
		Vector3 runtimeLastOffset = Vector3::AnyInit(0.0f);
		// 前フレームの揺れオフセットがTransformに残っているか
		bool runtimeApplied = false;
	};

	//============================================================================
	//	CameraControllerComponent structure
	//	カメラの追従、注視、揺れを管理するコンポーネント
	//============================================================================
	struct CameraControllerComponent {

		// コントローラー全体の有効状態
		bool enabled = true;
		// 適用する制御方法
		CameraControlMode mode = CameraControlMode::FollowLookAt;

		// ターゲット追従設定
		CameraFollowSettings follow{};
		// ターゲット注視設定
		CameraLookAtSettings lookAt{};
		// カメラ揺れ設定
		CameraShakeSettings shake{};
	};

	// 制御方法を表示用文字列へ変換する
	const char* ToString(CameraControlMode mode);
	// 表示用文字列から制御方法へ変換する
	CameraControlMode CameraControlModeFromString(const std::string& text);
	// ランタイム用の揺れを開始する
	void RequestCameraShake(CameraControllerComponent& component,
		float amplitude, float duration, float frequency = -1.0f);

	// jsonからコンポーネントへ変換する
	void from_json(const nlohmann::json& in, CameraControllerComponent& component);
	// コンポーネントからjsonへ変換する
	void to_json(nlohmann::json& out, const CameraControllerComponent& component);

	ENGINE_REGISTER_COMPONENT(CameraControllerComponent, "CameraController");
} // Engine
