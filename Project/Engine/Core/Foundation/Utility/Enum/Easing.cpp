#include "Easing.h"

// c++
#include <cmath>
#include <numbers>

// 曲線ごとの補間

float EaseInSine(float t) {

	// 正弦曲線で加速する
	return 1.0f - std::cos((t * std::numbers::pi_v<float>) / 2.0f);
}

float EaseOutSine(float t) {

	// 正弦曲線で減速する
	return std::sin((t * std::numbers::pi_v<float>) / 2.0f);
}

float EaseInOutSine(float t) {

	// 正弦曲線で加減速する
	return -(std::cos(std::numbers::pi_v<float> * t) - 1.0f) / 2.0f;
}

float EaseInQuad(float t) {

	// 二次曲線で加速する
	return t * t;
}

float EaseOutQuad(float t) {

	// 二次曲線で減速する
	return 1.0f - (1.0f - t) * (1.0f - t);
}

float EaseInOutQuad(float t) {

	// 二次曲線で加減速する
	return t < 0.5f ? 2.0f * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 2.0f) / 2.0f;
}

float EaseInCubic(float t) {

	// 三次曲線で加速する
	return t * t * t;
}

float EaseOutCubic(float t) {

	// 三次曲線で減速する
	return 1.0f - std::powf(1.0f - t, 3.0f);
}

float EaseInOutCubic(float t) {

	// 三次曲線で加減速する
	return t < 0.5f ? 4.0f * t * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 3.0f) / 2.0f;
}

float EaseInQuart(float t) {

	// 四次曲線で加速する
	return t * t * t * t;
}

float EaseOutQuart(float t) {

	// 四次曲線で減速する
	return 1.0f - std::powf(1.0f - t, 4.0f);
}

float EaseInOutQuart(float t) {

	// 四次曲線で加減速する
	return t < 0.5f ? 8.0f * t * t * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 4.0f) / 2.0f;
}

float EaseInQuint(float t) {

	// 五次曲線で加速する
	return t * t * t * t * t;
}

float EaseOutQuint(float t) {

	// 五次曲線で減速する
	return 1.0f - std::powf(1.0f - t, 5.0f);
}

float EaseInOutQuint(float t) {

	// 五次曲線で加減速する
	return t < 0.5f ? 16.0f * t * t * t * t * t : 1.0f - std::powf(-2.0f * t + 2.0f, 5.0f) / 2.0f;
}

float EaseInExpo(float t) {

	// 指数曲線で加速する
	return t == 0.0f ? 0.0f : std::powf(2.0f, 10.0f * t - 10.0f);
}

float EaseOutExpo(float t) {

	// 指数曲線で減速する
	return t == 1.0f ? 1.0f : 1.0f - std::powf(2.0f, -10.0f * t);
}

float EaseInOutExpo(float t) {

	// 指数曲線で加減速する
	if (t == 0.0f) {
		return 0.0f;
	}
	if (t == 1.0f) {
		return 1.0f;
	}
	return t < 0.5f ? std::powf(2.0f, 20 * t - 10.0f) / 2.0f : (2.0f - std::powf(2.0f, -20.0f * t + 10.0f)) / 2.0f;
}

float EaseInCirc(float t) {

	// 円弧で加速する
	return 1.0f - std::sqrtf(1.0f - std::powf(t, 2.0f));
}

float EaseOutCirc(float t) {

	// 円弧で減速する
	return std::sqrtf(1.0f - std::powf(t - 1.0f, 2.0f));
}

float EaseInOutCirc(float t) {

	// 円弧で加減速する
	return t < 0.5f ? (1.0f - std::sqrtf(1.0f - std::powf(2.0f * t, 2.0f))) / 2.0f
					: (std::sqrtf(1.0f - std::powf(-2.0f * t + 2.0f, 2.0f)) + 1.0f) / 2.0f;
}

float EaseOutBack(float t) {

	// 終点を越えてから戻る
	constexpr float c1 = 1.70158f;
	constexpr float c3 = c1 + 1.0f;

	return 1.0f + c3 * std::powf(t - 1.0f, 3) + c1 * std::powf(t - 1.0f, 2);
}

float EaseInBack(float t) {

	// 始点で逆方向へ振れる
	constexpr float c1 = 1.70158f;
	constexpr float c3 = c1 + 1.0f;

	return c3 * std::powf(t, 3) - c1 * std::powf(t, 2);
}

float EaseInBounce(float t) {

	// 始点で跳ね返る
	return 1.0f - EaseOutBounce(1.0f - t);
}

float EaseOutBounce(float t) {

	// 終点で跳ね返る
	constexpr float n1 = 7.5625f;
	constexpr float d1 = 2.75f;

	if (t < 1.0f / d1) {
		return n1 * t * t;
	} else if (t < 2.0f / d1) {
		t -= 1.5f / d1;
		return n1 * t * t + 0.75f;
	} else if (t < 2.5f / d1) {
		t -= 2.25f / d1;
		return n1 * t * t + 0.9375f;
	} else {
		t -= 2.625f / d1;
		return n1 * t * t + 0.984375f;
	}
}

float EaseInOutBounce(float t) {

	// 始点と終点で跳ね返る
	if (t < 0.5f) {
		return EaseInBounce(t * 2.0f) * 0.5f;
	} else {
		return EaseOutBounce(t * 2.0f - 1.0f) * 0.5f + 0.5f;
	}
}

float EasedValue(EasingType easingType, float t) {

	// 指定された曲線を評価する
	switch (easingType) {
	case EasingType::Linear:
		return t;
	case EasingType::EaseInSine:
		return EaseInSine(t);
	case EasingType::EaseOutSine:
		return EaseOutSine(t);
	case EasingType::EaseInOutSine:
		return EaseInOutSine(t);
	case EasingType::EaseInQuad:
		return EaseInQuad(t);
	case EasingType::EaseOutQuad:
		return EaseOutQuad(t);
	case EasingType::EaseInOutQuad:
		return EaseInOutQuad(t);
	case EasingType::EaseInCubic:
		return EaseInCubic(t);
	case EasingType::EaseOutCubic:
		return EaseOutCubic(t);
	case EasingType::EaseInOutCubic:
		return EaseInOutCubic(t);
	case EasingType::EaseInQuart:
		return EaseInQuart(t);
	case EasingType::EaseOutQuart:
		return EaseOutQuart(t);
	case EasingType::EaseInOutQuart:
		return EaseInOutQuart(t);
	case EasingType::EaseInQuint:
		return EaseInQuint(t);
	case EasingType::EaseOutQuint:
		return EaseOutQuint(t);
	case EasingType::EaseInOutQuint:
		return EaseInOutQuint(t);
	case EasingType::EaseInExpo:
		return EaseInExpo(t);
	case EasingType::EaseOutExpo:
		return EaseOutExpo(t);
	case EasingType::EaseInOutExpo:
		return EaseInOutExpo(t);
	case EasingType::EaseInCirc:
		return EaseInCirc(t);
	case EasingType::EaseOutCirc:
		return EaseOutCirc(t);
	case EasingType::EaseInOutCirc:
		return EaseInOutCirc(t);
	case EasingType::EaseInBack:
		return EaseInBack(t);
	case EasingType::EaseOutBack:
		return EaseOutBack(t);
	case EasingType::EaseInBounce:
		return EaseInBounce(t);
	case EasingType::EaseOutBounce:
		return EaseOutBounce(t);
	case EasingType::EaseInOutBounce:
		return EaseInOutBounce(t);
	default:
		return t;
	}
}
