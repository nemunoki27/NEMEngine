#pragma once

//============================================================================
//	include
//============================================================================
#include "AnimationPropertyRegistry.h"
#include <Engine/Core/Rendering/Materials/MaterialParameter.h>

namespace Engine {

	// 未設定のパラメータに対応する初期値を返す
	AnimationPropertyValue ZeroAnimationValue(AnimationValueType type);

	// Material値をアニメーション値へ変換する
	bool MaterialValueToAnimation(const MaterialParameterValue& value, AnimationValueType type, AnimationPropertyValue& out);

	// アニメーション値をMaterial値へ変換する
	MaterialParameterValue AnimationValueToMaterial(const AnimationPropertyValue& value);
} // Engine
