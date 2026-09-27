#include "AssimpMaterialTextureExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>

// c++
#include <algorithm>

namespace Engine::AssimpMaterialTextureExtractor {

	std::string Extract(const aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) {

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

	PBRTextureReferences ExtractPBR(const aiMaterial* material) {

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

	ImportedMeshTextureSet ExtractResolved(const aiMaterial* material, const TextureAssetResolver& resolver) {

		ImportedMeshTextureSet result{};
		if (!material) {
			return result;
		}
		const std::string baseColor = Extract(material, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
		const std::string normal = Extract(material, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA });
		const std::string height = Extract(material, { aiTextureType_HEIGHT });
		PBRTextureReferences pbr = ExtractPBR(material);
		// HEIGHTはNormal未指定時にバンプ、併存時に変位へ使う
		if (pbr.displacement.empty() && !normal.empty()) {
			pbr.displacement = height;
		}
		result.baseColorTexturePath = resolver.ResolveAssetPath(baseColor);
		result.normalTexturePath = resolver.ResolveNormalAssetPath(normal.empty() ? height : normal, baseColor);
		result.metallicRoughnessTexturePath = resolver.ResolveAssetPath(pbr.metallicRoughness);
		result.metallicTexturePath = resolver.ResolveAssetPath(pbr.metallic);
		result.roughnessTexturePath = resolver.ResolveAssetPath(pbr.roughness);
		result.displacementTexturePath = resolver.ResolveAssetPath(pbr.displacement);
		result.specularTexturePath = resolver.ResolveAssetPath(Extract(material, { aiTextureType_SPECULAR }));
		result.opacityTexturePath = resolver.ResolveAssetPath(Extract(material, { aiTextureType_OPACITY }));
		result.emissiveTexturePath = resolver.ResolveAssetPath(
			Extract(material, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR }));
		result.occlusionTexturePath = resolver.ResolveAssetPath(
			Extract(material, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP }));
		return result;
	}

	std::vector<std::string> CollectResolvedPaths(const aiMaterial* material, const TextureAssetResolver& resolver) {

		const auto textures = ExtractResolved(material, resolver);
		std::vector<std::string> paths;
		// 同じ画像を複数用途から二重登録しない
		for (const auto* path : { &textures.baseColorTexturePath, &textures.normalTexturePath,
			&textures.metallicRoughnessTexturePath, &textures.metallicTexturePath, &textures.roughnessTexturePath,
			&textures.displacementTexturePath, &textures.specularTexturePath, &textures.opacityTexturePath,
			&textures.emissiveTexturePath, &textures.occlusionTexturePath }) {

			if (!path->empty() && std::find(paths.begin(), paths.end(), *path) == paths.end()) {
				paths.emplace_back(*path);
			}
		}
		return paths;
	}

} // Engine::AssimpMaterialTextureExtractor
