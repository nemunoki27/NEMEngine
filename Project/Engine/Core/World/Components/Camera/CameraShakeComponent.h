#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Components/Registry/ComponentTypeRegistry.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>
#include <Engine/Core/Foundation/Math/Math.h>

namespace Engine {

	//============================================================================
	//	CameraShakeComponent struct
	//	カメラシェイク
	//============================================================================

	struct CameraShakeComponent {

		// 有効/無効フラグ、デフォルトでfalse、trueで開始
		bool enable = false;

		// シェイクの長さ
		float duration = 1.0f;
		// 再生時間
		float runtimeTime = 0.0f;
		// 前フレームに適用したオフセット
		Vector3 runtimeOffset{};
		// 再生開始を検知するランタイムフラグ
		bool runtimeActive = false;
		// イージング
		EasingType easingType = EasingType::Linear;

		// シェイクの強さ
		Vector3 strength = Vector3::AnyInit(4.0f);
	};

	// json変換
	void from_json(const nlohmann::json& in, CameraShakeComponent& component);
	void to_json(nlohmann::json& out, const CameraShakeComponent& component);

} // Engine
