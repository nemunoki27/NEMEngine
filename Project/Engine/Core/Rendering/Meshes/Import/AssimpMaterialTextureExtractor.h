#pragma once

//============================================================================
//	include
//============================================================================
#include <string>
#include <initializer_list>
#include <assimp/material.h>

namespace Engine::AssimpMaterialTextureExtractor {

	// PBR用途ごとに正規化したテクスチャ参照
	struct PBRTextureReferences {

		std::string metallicRoughness{};
		std::string metallic{};
		std::string roughness{};
		std::string displacement{};
	};

	// Assimpのマテリアルから指定した複数のテクスチャタイプのうち最初に見つかったテクスチャのパスを参照文字列として取得する
	std::string Extract(aiMaterial* material, std::initializer_list<aiTextureType> textureTypes);
	// AssimpのPBRテクスチャを統合MRと個別M/Rへ重複なく分類する
	PBRTextureReferences ExtractPBR(aiMaterial* material);

} // Engine::AssimpMaterialTextureExtractor
