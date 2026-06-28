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

} // Engine::AssimpMaterialTextureExtractor
