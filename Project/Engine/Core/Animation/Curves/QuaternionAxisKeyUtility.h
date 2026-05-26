#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Animation/Curves/AnimationCurve.h>

namespace Engine::QuaternionAxisKeyUtility {

	// デフォルトの軸キーを作成する
	CurveQuaternionAxisKey MakeDefault();

	// 軸キーから正規化された方向ベクトルを取得する
	Vector3 GetAxisDirection(const CurveQuaternionAxisKey& key);

	// 軸キーを正常な値に補正する
	CurveQuaternionAxisKey Sanitize(const CurveQuaternionAxisKey& key);

} // Engine::QuaternionAxisKeyUtility
