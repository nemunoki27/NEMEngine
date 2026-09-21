#include "MeshImportUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <algorithm>
#include <string_view>

#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/material.h>
#include <assimp/GltfMaterial.h>

//============================================================================
//	MeshImportUtility functions
//============================================================================
std::string Engine::MeshImportUtility::BuildSubMeshName(
	const aiMesh* mesh, uint32_t meshIndex, const aiMaterial* material) {

	if (mesh && mesh->mName.length > 0) {
		return mesh->mName.C_Str();
	}
	if (material) {
		aiString materialName;
		if (material->Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS && materialName.length > 0) {
			return materialName.C_Str();
		}
	}
	return "SubMesh_" + std::to_string(meshIndex);
}

Engine::MeshImportUtility::ImportedMaterialSurface
Engine::MeshImportUtility::ReadMaterialSurface(const aiMaterial* material) {

	ImportedMaterialSurface result{};
	if (!material) {
		return result;
	}

	// glTFのalphaModeはテクスチャ中のα値より優先される
	aiString alphaMode;
	if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS) {

		const std::string_view mode = alphaMode.C_Str();
		if (mode == "MASK") {
			result.surfaceMode = MaterialSurfaceMode::Masked;
			ai_real cutoff = 0.5f;
			if (material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS) {
				result.alphaCutoff = std::clamp(static_cast<float>(cutoff), 0.0f, 1.0f);
			}
		} else if (mode == "BLEND") {
			result.surfaceMode = MaterialSurfaceMode::Transparent;
		} else {
			result.surfaceMode = MaterialSurfaceMode::Opaque;
		}
		return result;
	}

	// glTF以外は定数Opacityと専用Opacityテクスチャから安全側で推定する
	ai_real opacity = 1.0f;
	if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS &&
		static_cast<float>(opacity) < 1.0f) {
		result.surfaceMode = MaterialSurfaceMode::Transparent;
		return result;
	}
	if (material->GetTextureCount(aiTextureType_OPACITY) > 0) {
		result.surfaceMode = MaterialSurfaceMode::Masked;
	}
	return result;
}

Engine::MeshNode Engine::MeshImportUtility::ReadMeshNode(const aiNode* node) {

	MeshNode result{};
	if (!node) {
		return result;
	}

	aiVector3D scale, translate;
	aiQuaternion rotate;
	// Assimpのノード変換行列をスケール、回転、平行移動に分解する
	node->mTransformation.Decompose(scale, rotate, translate);

	// 符号反転でエンジン座標系へ合わせる
	result.transform.scale = { scale.x, scale.y, scale.z };
	result.transform.rotation = { rotate.x, -rotate.y, -rotate.z, rotate.w };
	result.transform.translation = { -translate.x, translate.y, translate.z };
	result.localMatrix = Matrix4x4::MakeAffineMatrix(result.transform.scale,
		result.transform.rotation, result.transform.translation);
	result.name = node->mName.C_Str();
	return result;
}

Engine::MeshNode Engine::MeshImportUtility::ReadMeshNodeTree(const aiNode* node) {

	if (!node) {
		return {};
	}

	MeshNode result = ReadMeshNode(node);

	// 子ノードも再帰的に読み込む
	result.children.resize(node->mNumChildren);
	for (uint32_t i = 0; i < node->mNumChildren; ++i) {

		result.children[i] = ReadMeshNodeTree(node->mChildren[i]);
	}
	return result;
}
