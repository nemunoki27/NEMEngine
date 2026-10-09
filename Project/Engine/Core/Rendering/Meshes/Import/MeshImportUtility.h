#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/MeshNode.h>
#include <Engine/Core/Assets/RenderComponentTypes.h>

// c++
#include <string>
#include <cstdint>

// front
struct aiNode;
struct aiMesh;
struct aiMaterial;

namespace Engine { class TextureAssetResolver; }

namespace Engine::MeshImportUtility {

	// モデルマテリアルから読み取った表面設定
	struct ImportedMaterialSurface {

		MaterialSurfaceMode surfaceMode = MaterialSurfaceMode::Auto;
		float alphaCutoff = 0.5f;
	};

	// モデルに明示されたMaterial係数
	struct ImportedMaterialFactors {

		Color4 baseColor = Color4::White();
		Color4 emissive = Color4(0.0f, 0.0f, 0.0f, 1.0f);
		float metallic = 0.0f;
		float roughness = 1.0f;
		bool hasBaseColor = false;
		bool hasEmissive = false;
		bool hasMetallic = false;
		bool hasRoughness = false;
	};

	// Mesh名・Material名・連番から表示名を決める
	std::string BuildSubMeshName(const aiMesh* mesh, uint32_t meshIndex, const aiMaterial* material);
	// 三角形を持つMeshだけを描画と編集の対象にする
	bool HasTriangleGeometry(const aiMesh* mesh);
	// glTFや汎用Opacity情報から表面方式を読み取る
	ImportedMaterialSurface ReadMaterialSurface(const aiMaterial* material,
		const TextureAssetResolver* textureResolver = nullptr);
	// PBR係数を優先し、従来Materialの色とOpacityを補完する
	ImportedMaterialFactors ReadMaterialFactors(const aiMaterial* material);

	// ノードをエンジン座標へ変換する
	MeshNode ReadMeshNode(const aiNode* node);
	// ノード階層を再帰的に読み込む
	MeshNode ReadMeshNodeTree(const aiNode* node);

}
