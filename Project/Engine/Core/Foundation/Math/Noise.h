#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>

namespace Math {

	//============================================================================
	//	Noise
	//============================================================================

	// 3Dパーリンノイズ、-1から1の値を返す
	float PerlinNoise3D(float x, float y, float z);

	// 位置から3成分のノイズベクトルを作る、パーティクルの乱流などに使う
	Engine::Vector3 PerlinNoiseVector3(const Engine::Vector3& position, float frequency);
} // Math
