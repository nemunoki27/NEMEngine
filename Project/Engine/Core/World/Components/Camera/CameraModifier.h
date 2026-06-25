#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine {

	//============================================================================
	//	CameraModifier struct
	//	カメラパラメータの上書き制御
	//============================================================================

	// 画面シェイク
	enum class CameraShakeMode {

		Impact, // ランダム揺れ
		Noise,  // 有機ノイズ揺れ
	};
	struct CameraScreenShake {

		// 有効/無効フラグ、デフォルトでfalse、trueで開始
		bool enable = false;
		CameraShakeMode mode = CameraShakeMode::Impact;

		// シェイクの長さ
		float duration = 1.0f;
		// 再生時間
		float runtimeTime = 0.0f;
		// イージング
		EasingType easingType = EasingType::Linear;

		// シェイクの強さ
		Vector3 strength = Vector3::AnyInit(4.0f);

		// TODO Noiseパラメータ
	};
	// 回転制限
	struct CameraPitchControl {

		// 有効/無効フラグ
		bool enable = true;

		// 角度制限値
		float minPitch = 0.0f;
		float maxPitch = 0.0f;
	};

	struct CameraModifier {

		// シェイク
		CameraScreenShake shake{};
		CameraPitchControl pitchControl{};
	};

	// json変換
	void from_json(const nlohmann::json& in, CameraModifier& component);
	void to_json(nlohmann::json& out, const CameraModifier& component);

	ENGINE_REGISTER_COMPONENT(CameraModifier, "CameraModifier");
} // Engine