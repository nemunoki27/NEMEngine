#include "AssimpMaterialTextureExtractor.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Meshes/GPUResource/MeshResourceTypes.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

// c++
#include <algorithm>

namespace Engine::AssimpMaterialTextureExtractor {

	std::string Extract(const aiMaterial* material, std::initializer_list<aiTextureType> textureTypes) {

		if (!material) {
			return {};
		}

		// 指定順に存在するTextureを読む
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
		result.metallic = Extract(material, {aiTextureType_METALNESS});
		result.roughness = Extract(material, {aiTextureType_DIFFUSE_ROUGHNESS});
		result.displacement = Extract(material, {aiTextureType_DISPLACEMENT});

		// glTFは同じ画像をMetallicとRoughnessへ公開するため統合テクスチャとして扱う
		if (!result.metallic.empty() && result.metallic == result.roughness) {
			result.metallicRoughness = result.metallic;
			result.metallic.clear();
			result.roughness.clear();
		}

		// 型情報を持たないORM等は個別テクスチャと重複しない場合だけ統合マップへ流す
		const std::string unknown = Extract(material, {aiTextureType_UNKNOWN});
		if (result.metallicRoughness.empty() && !unknown.empty() && unknown != result.metallic && unknown != result.roughness) {

			result.metallicRoughness = unknown;
		}
		return result;
	}

	namespace {

		// Asset登録と実ファイル収集でMaterialの用途判定を共有する
		ImportedMeshTextureSet ResolveTextures(
			const aiMaterial* material, const TextureAssetResolver& resolver, bool filePaths) {

			ImportedMeshTextureSet result{};
			if (!material) {
				return result;
			}
			const auto resolve = [&](const std::string& reference) {
				return filePaths ? Algorithm::PathToUTF8(resolver.ResolveFilePath(reference))
								 : resolver.ResolveAssetPath(reference);
			};
			const std::string baseColor = Extract(material, {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE});
			const std::string normal = Extract(material, {aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA});
			const std::string height = Extract(material, {aiTextureType_HEIGHT});
			PBRTextureReferences pbr = ExtractPBR(material);
			// HEIGHTはNormal未指定時にバンプ、併存時に変位へ使う
			if (pbr.displacement.empty() && !normal.empty()) {
				pbr.displacement = height;
			}
			result.baseColorTexturePath = resolve(baseColor);
			result.normalTexturePath =
				filePaths ? Algorithm::PathToUTF8(resolver.ResolveNormalFilePath(normal.empty() ? height : normal, baseColor))
						  : resolver.ResolveNormalAssetPath(normal.empty() ? height : normal, baseColor);
			result.metallicRoughnessTexturePath = resolve(pbr.metallicRoughness);
			result.metallicTexturePath = resolve(pbr.metallic);
			result.roughnessTexturePath = resolve(pbr.roughness);
			result.displacementTexturePath = resolve(pbr.displacement);
			result.specularTexturePath = resolve(Extract(material, {aiTextureType_SPECULAR}));
			result.opacityTexturePath = resolve(Extract(material, {aiTextureType_OPACITY}));
			result.emissiveTexturePath = resolve(Extract(material, {aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR}));
			result.occlusionTexturePath = resolve(Extract(material, {aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP}));
			return result;
		}

		// 同じ画像を複数用途から重複して返さない
		std::vector<std::string> CollectPaths(const ImportedMeshTextureSet& textures) {

			std::vector<std::string> paths;
			for (const auto* path : {&textures.baseColorTexturePath, &textures.normalTexturePath,
					 &textures.metallicRoughnessTexturePath, &textures.metallicTexturePath, &textures.roughnessTexturePath,
					 &textures.displacementTexturePath, &textures.specularTexturePath, &textures.opacityTexturePath,
					 &textures.emissiveTexturePath, &textures.occlusionTexturePath}) {

				if (!path->empty() && std::find(paths.begin(), paths.end(), *path) == paths.end()) {
					paths.emplace_back(*path);
				}
			}
			return paths;
		}

	}

	ImportedMeshTextureSet ExtractResolved(const aiMaterial* material, const TextureAssetResolver& resolver) {

		return ResolveTextures(material, resolver, false);
	}

	std::vector<std::string> CollectResolvedPaths(const aiMaterial* material, const TextureAssetResolver& resolver) {

		return CollectPaths(ExtractResolved(material, resolver));
	}

	std::vector<std::filesystem::path> CollectResolvedFiles(const aiMaterial* material, const TextureAssetResolver& resolver) {

		std::vector<std::filesystem::path> files;
		for (const auto& path : CollectPaths(ResolveTextures(material, resolver, true))) {
			files.emplace_back(Algorithm::PathFromUTF8(path));
		}
		return files;
	}

}
