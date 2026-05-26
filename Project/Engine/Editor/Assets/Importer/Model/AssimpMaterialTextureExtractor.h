#pragma once

//============================================================================
//	include
//============================================================================
#include <string>
#include <initializer_list>
#include <assimp/material.h>

namespace Engine::AssimpMaterialTextureExtractor {

	// Assimpのマテリアルから指定した複数のテクスチャタイプのうち、最初に見つかったテクスチャのパス（参照文字列）を取得する。
	std::string Extract(aiMaterial* material, std::initializer_list<aiTextureType> textureTypes);

} // Engine::AssimpMaterialTextureExtractor
