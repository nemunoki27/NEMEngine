#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/MathLib/Color.h>
#include <Engine/MathLib/Matrix4x4.h>

// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	MeshShaderSharedTypes structures
	//============================================================================

	struct SubMeshConstants {

		uint32_t indexOffset = 0;
		uint32_t indexCount = 0;
		uint32_t subMeshIndex = 0;
		uint32_t _pad0 = 0;
	};

	struct MeshDrawConstants {

		uint32_t meshletCount = 0;
		uint32_t subMeshCount = 0;
		uint32_t instanceCount = 0;
		uint32_t _pad0 = 0;
	};

	struct MeshSubMeshShaderData {

		uint32_t baseColorTextureIndex = 0;
		float _pad[3] = { 0.0f, 0.0f, 0.0f };

		// サブメッシュごとのローカル行列
		Matrix4x4 localMatrix = Matrix4x4::Identity();

		// インポート時の色
		Color importedBaseColor = Color::White();
		// エディタ編集色
		Color color = Color::White();
		// サブメッシュごとのUV
		Matrix4x4 uvMatrix = Matrix4x4::Identity();
	};
} // Engine