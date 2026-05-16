#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Math/Vector2.h>
#include <Engine/Core/Math/Vector3.h>

// c++
#include <vector>

namespace Engine {

	//============================================================================
	//	Axis structures
	//============================================================================

	// 軸
	enum class Axis {

		X,
		Y,
		Z
	};

	// 軸方向の取得
	Vector3 GetDirection(const std::vector<Axis>& axes);
}