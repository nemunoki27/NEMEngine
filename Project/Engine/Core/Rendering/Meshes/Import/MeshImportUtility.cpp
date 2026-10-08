#include "MeshImportUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Math/Matrix4x4.h>

// c++
#include <algorithm>
#include <string_view>

// assimp
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

bool Engine::MeshImportUtility::HasTriangleGeometry(const aiMesh* mesh) {

	if (!mesh || !mesh->HasPositions() || !mesh->HasFaces()) {
		return false;
	}
	// 点・線だけのMeshをSubMeshや骨の対応へ含めない
	return std::any_of(mesh->mFaces, mesh->mFaces + mesh->mNumFaces,
		[](const aiFace& face) { return face.mNumIndices == 3; });
}

Engine::MeshImportUtility::ImportedMaterialSurface
Engine::MeshImportUtility::ReadMaterialSurface(const aiMaterial* material) {

	ImportedMaterialSurface result{};
	if (!material) {
		return result;
	}

	// glTFのAlphaModeを優先する
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

	// Opacityと専用Textureから表面方式を推定する
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

Engine::MeshImportUtility::ImportedMaterialFactors
Engine::MeshImportUtility::ReadMaterialFactors(const aiMaterial* material) {

	ImportedMaterialFactors result{};
	if (!material) {
		return result;
	}

	// PBRのAlphaへ汎用Opacityを重ねて適用しない
	aiColor4D baseColor{};
	if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {
		result.baseColor = Color4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
		result.hasBaseColor = true;
	} else {
		if (material->Get(AI_MATKEY_COLOR_DIFFUSE, baseColor) == AI_SUCCESS) {
			result.baseColor = Color4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
			result.hasBaseColor = true;
		}
		// OBJ等の透明度は色とは別のプロパティに格納される
		ai_real opacity = 1.0f;
		if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
			result.baseColor.a = std::clamp(static_cast<float>(opacity), 0.0f, 1.0f);
			result.hasBaseColor = true;
		}
	}
	aiColor3D emissive{};
	if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
		result.emissive = Color4(emissive.r, emissive.g, emissive.b, 1.0f);
		result.hasEmissive = true;
	}
	ai_real metallic = 0.0f;
	if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
		result.metallic = static_cast<float>(metallic);
		result.hasMetallic = true;
	}
	ai_real roughness = 1.0f;
	if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
		result.roughness = static_cast<float>(roughness);
		result.hasRoughness = true;
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
	// 行列をスケール・回転・平行移動に分解する
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
