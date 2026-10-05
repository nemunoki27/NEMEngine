#pragma once

//============================================================================
//	Easing
//============================================================================

// 正弦曲線で加速する
float EaseInSine(float t);
// 正弦曲線で減速する
float EaseOutSine(float t);
// 正弦曲線で加減速する
float EaseInOutSine(float t);
// 二次曲線で加速する
float EaseInQuad(float t);
// 二次曲線で減速する
float EaseOutQuad(float t);
// 二次曲線で加減速する
float EaseInOutQuad(float t);
// 三次曲線で加速する
float EaseInCubic(float t);
// 三次曲線で減速する
float EaseOutCubic(float t);
// 三次曲線で加減速する
float EaseInOutCubic(float t);
// 四次曲線で加速する
float EaseInQuart(float t);
// 四次曲線で減速する
float EaseOutQuart(float t);
// 四次曲線で加減速する
float EaseInOutQuart(float t);
// 五次曲線で加速する
float EaseInQuint(float t);
// 五次曲線で減速する
float EaseOutQuint(float t);
// 五次曲線で加減速する
float EaseInOutQuint(float t);
// 指数曲線で加速する
float EaseInExpo(float t);
// 指数曲線で減速する
float EaseOutExpo(float t);
// 指数曲線で加減速する
float EaseInOutExpo(float t);
// 円弧で加速する
float EaseInCirc(float t);
// 円弧で減速する
float EaseOutCirc(float t);
// 円弧で加減速する
float EaseInOutCirc(float t);
// 終点を越えてから戻る
float EaseOutBack(float t);
// 始点で逆方向へ振れる
float EaseInBack(float t);
// 始点で跳ね返る
float EaseInBounce(float t);
// 終点で跳ね返る
float EaseOutBounce(float t);
// 始点と終点で跳ね返る
float EaseInOutBounce(float t);

// 補間曲線の種類
enum class EasingType {

	// そのまま
	Linear,

	// 加速
	EaseInSine,
	EaseInQuad,
	EaseInCubic,
	EaseInQuart,
	EaseInQuint,
	EaseInExpo,
	EaseInCirc,
	EaseInBack,
	EaseInBounce,

	// 減速
	EaseOutSine,
	EaseOutQuad,
	EaseOutCubic,
	EaseOutQuart,
	EaseOutQuint,
	EaseOutExpo,
	EaseOutCirc,
	EaseOutBack,
	EaseOutBounce,

	// 加減速
	EaseInOutSine,
	EaseInOutQuad,
	EaseInOutCubic,
	EaseInOutQuart,
	EaseInOutQuint,
	EaseInOutExpo,
	EaseInOutCirc,
	EaseInOutBounce,
};

// 指定した曲線で補間値を求める
float EasedValue(EasingType easingType, float t);
