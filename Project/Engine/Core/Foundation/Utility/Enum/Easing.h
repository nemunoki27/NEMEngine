#pragma once

//============================================================================*/
//	include
//============================================================================*/

// c++
#include <cmath>
#define _USE_MATH_DEFINES
#include <math.h>
#include <numbers>
#include <string>

//============================================================================*/
//	Easing
//============================================================================*/

float EaseInSine(float t);
float EaseOutSine(float t);
float EaseInOutSine(float t);
float EaseInQuad(float t);
float EaseOutQuad(float t);
float EaseInOutQuad(float t);
float EaseInCubic(float t);
float EaseOutCubic(float t);
float EaseInOutCubic(float t);
float EaseInQuart(float t);
float EaseOutQuart(float t);
float EaseInOutQuart(float t);
float EaseInQuint(float t);
float EaseOutQuint(float t);
float EaseInOutQuint(float t);
float EaseInExpo(float t);
float EaseOutExpo(float t);
float EaseInOutExpo(float t);
float EaseInCirc(float t);
float EaseOutCirc(float t);
float EaseInOutCirc(float t);
float EaseOutBack(float t);
float EaseInBack(float t);
float EaseInBounce(float t);
float EaseOutBounce(float t);
float EaseInOutBounce(float t);

enum class EasingType {

	// そのまま
	Linear,

	// EaseIn
	EaseInSine,
	EaseInQuad,
	EaseInCubic,
	EaseInQuart,
	EaseInQuint,
	EaseInExpo,
	EaseInCirc,
	EaseInBack,
	EaseInBounce,

	// EaseOut
	EaseOutSine,
	EaseOutQuad,
	EaseOutCubic,
	EaseOutQuart,
	EaseOutQuint,
	EaseOutExpo,
	EaseOutCirc,
	EaseOutBack,
	EaseOutBounce,

	// EaseInOut
	EaseInOutSine,
	EaseInOutQuad,
	EaseInOutCubic,
	EaseInOutQuart,
	EaseInOutQuint,
	EaseInOutExpo,
	EaseInOutCirc,
	EaseInOutBounce,
};

float EasedValue(EasingType easingType, float t);

namespace Easing {

	// imgui選択
	void SelectEasingType(EasingType& easingType, const std::string& label = "label", float itemWidth = 200.0f);
}