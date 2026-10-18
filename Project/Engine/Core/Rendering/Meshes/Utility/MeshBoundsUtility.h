#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>

// c++
#include <span>

namespace Engine::MeshBoundsUtility {

	// 全頂点またはIndex範囲を囲む球を求める
	void Calculate(std::span<const MeshVertex> vertices, std::span<const uint32_t> indices, Vector3& center, float& radius);
	// 非一様ScaleとShearを含む変換後も球を欠かさない
	void TransformSphere(const Matrix4x4& matrix, Vector3& center, float& radius);
}
