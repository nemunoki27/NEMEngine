#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Vector3.h>
#include <Engine/MathLib/Quaternion.h>
#include <Engine/MathLib/Matrix4x4.h>

// c++
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	MeshNode structures
	//============================================================================

	struct NodeTransform {

		Vector3 scale = Vector3::AnyInit(1.0f);
		Quaternion rotation = Quaternion::Identity();
		Vector3 translation = Vector3::AnyInit(0.0f);
	};

	struct MeshNode {

		// ノード名
		std::string name;
		// ノードのローカル変換
		NodeTransform transform{};
		Matrix4x4 localMatrix = Matrix4x4::Identity();

		// 子ノード
		std::vector<MeshNode> children;
	};
} // Engine