#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Math.h>
#include <Engine/Core/Foundation/Utility/Enum/Easing.h>

// c++
#include <cstdint>
#include <variant>

namespace Engine {

	enum class SimpleAnimationMode : uint8_t { Interpolate, Sin, Cos };
	enum class SimpleAnimationRepeat : uint8_t { Once, Loop, PingPong };
	enum class SimpleAnimationRotationMode : uint8_t { AxisAngle, Euler };
	using SimpleAnimationValue = std::variant<float, Vector2, Vector3, Vector4, Color3, Color4, Quaternion, int32_t>;

	// 単一の値を補間または周期波形で動かす設定
	struct SimpleAnimationSettings {

		SimpleAnimationMode mode = SimpleAnimationMode::Interpolate;
		SimpleAnimationRepeat repeat = SimpleAnimationRepeat::Once;
		uint32_t cycleCount = 0;
		SimpleAnimationValue start = 0.0f;
		SimpleAnimationValue end = 1.0f;
		SimpleAnimationValue center = 0.0f;
		SimpleAnimationValue amplitude = 1.0f;
		Vector3 angularAmplitude{};
		SimpleAnimationRotationMode rotationMode = SimpleAnimationRotationMode::AxisAngle;
		Vector3 startEuler{};
		Vector3 endEuler{};
		Vector3 centerEuler{};
		EasingType easing = EasingType::Linear;
		double duration = 1.0;
		double period = 1.0;
		double phaseDegrees = 0.0;
		double delay = 0.0;
		double speed = 1.0;
		bool unscaledTime = false;
		bool startFromCurrent = false;
		double blendDuration = 0.0;
	};

	// 評価だけでは再生時計や対象の値を変更しない
	struct SimpleAnimationSample {

		SimpleAnimationValue value = 0.0f;
		uint64_t completedCycles = 0;
		bool finished = false;
		bool valid = false;
	};

	// 開始待ちを含む再生時刻から値を求める
	SimpleAnimationSample EvaluateSimpleAnimation(const SimpleAnimationSettings& settings, double time);
	SimpleAnimationValue BlendSimpleAnimation(const SimpleAnimationValue& from, const SimpleAnimationValue& to, float progress);
	SimpleAnimationSettings MakeSimpleAnimationSettings(size_t type);
	bool IsValidSimpleAnimation(const SimpleAnimationSettings& settings);
	void to_json(nlohmann::json& out, const SimpleAnimationSettings& settings);
	void from_json(const nlohmann::json& in, SimpleAnimationSettings& settings);
}
