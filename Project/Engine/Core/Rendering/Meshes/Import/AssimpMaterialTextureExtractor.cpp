#include "AssimpMaterialTextureExtractor.h"

namespace Engine::AssimpMaterialTextureExtractor {

	std::string Extract(aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) {

		if (!material) {
			return {};
		}

		for (aiTextureType type : textureTypes) {

			if (material->GetTextureCount(type) == 0) {
				continue;
			}

			aiString textureName;
			if (material->GetTexture(type, 0, &textureName) == AI_SUCCESS && 0 < textureName.length) {
				return textureName.C_Str();
			}
		}

		return {};
	}

	PBRTextureReferences ExtractPBR(aiMaterial* material) {

		PBRTextureReferences result{};
		result.metallic = Extract(material, { aiTextureType_METALNESS });
		result.roughness = Extract(material, { aiTextureType_DIFFUSE_ROUGHNESS });
		result.displacement = Extract(material, { aiTextureType_DISPLACEMENT });

		// glTFは同じ画像をMetallicとRoughnessへ公開するため統合テクスチャとして扱う
		if (!result.metallic.empty() && result.metallic == result.roughness) {
			result.metallicRoughness = result.metallic;
			result.metallic.clear();
			result.roughness.clear();
		}

		// 型情報を持たないORM等は個別テクスチャと重複しない場合だけ統合マップへ流す
		const std::string unknown = Extract(material, { aiTextureType_UNKNOWN });
		if (result.metallicRoughness.empty() &&
			!unknown.empty() && unknown != result.metallic &&
			unknown != result.roughness) {

			result.metallicRoughness = unknown;
		}
		return result;
	}

} // Engine::AssimpMaterialTextureExtractor
