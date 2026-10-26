#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Vector3.h>

// c++
#include <istream>
#include <string>
#include <vector>

namespace Engine {

	struct VectorFieldData {

		Vector3I resolution{1, 1, 1};
		Vector3 minimum{-0.5f, -0.5f, -0.5f};
		Vector3 maximum{0.5f, 0.5f, 0.5f};
		std::vector<Vector3> vectors;
	};

	// X方向を最短軸とするFGA格子を読み込み、失敗時は出力を保持する
	bool ReadFGA(std::istream& stream, VectorFieldData& field, std::string& reason);
	// 格子の範囲外をゼロとして三線形補間する
	Vector3 SampleVectorField(const VectorFieldData& field, const Vector3& position);
}
