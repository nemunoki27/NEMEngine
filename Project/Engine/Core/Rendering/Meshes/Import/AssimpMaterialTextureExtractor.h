#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <filesystem>
#include <string>
#include <initializer_list>
#include <vector>

// assimp
#include <assimp/material.h>

namespace Engine {

	class TextureAssetResolver;
	struct ImportedMeshTextureSet;
}

namespace Engine::AssimpMaterialTextureExtractor {

	// PBR用途ごとに正規化したテクスチャ参照
	struct PBRTextureReferences {

		std::string metallicRoughness{};
		std::string metallic{};
		std::string roughness{};
		std::string displacement{};
	};

	// 指定したTexture用途から最初の参照を取得する
	std::string Extract(const aiMaterial* material, std::initializer_list<aiTextureType> textureTypes);
	// AssimpのPBRテクスチャを統合MRと個別M/Rへ重複なく分類する
	PBRTextureReferences ExtractPBR(const aiMaterial* material);
	// 標準Materialの外部Textureを用途別に解決する
	ImportedMeshTextureSet ExtractResolved(const aiMaterial* material, const TextureAssetResolver& resolver);
	// 解決済み画像を登録・配布用に重複なく列挙する
	std::vector<std::string> CollectResolvedPaths(const aiMaterial* material, const TextureAssetResolver& resolver);

	// 同じ用途判定からProject外を含む実ファイルを列挙する
	std::vector<std::filesystem::path> CollectResolvedFiles(const aiMaterial* material, const TextureAssetResolver& resolver);

}
