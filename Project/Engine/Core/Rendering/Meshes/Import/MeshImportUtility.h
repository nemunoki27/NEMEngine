#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/MeshNode.h>

// c++
#include <string>
#include <cstdint>

// front
struct aiNode;
struct aiMesh;
struct aiMaterial;

namespace Engine::MeshImportUtility {

	// サブメッシュの表示名を構築する、メッシュ名→マテリアル名→連番の順で決める
	std::string BuildSubMeshName(const aiMesh* mesh, uint32_t meshIndex, const aiMaterial* material);

	// assimpのノード階層をエンジン座標系へ変換して再帰的に読み込む
	MeshNode ReadMeshNodeTree(const aiNode* node);

} // Engine::MeshImportUtility
