#include "MeshSubMeshAuthoring.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Editor/Assets/Importer/Model/AssimpMaterialTextureExtractor.h>

//============================================================================
//	MeshSubMeshAuthoring classMethods
//============================================================================
namespace {

	// モデル既定テクスチャを未設定のparameterOverridesへAssetIDとして入れる、設定済みは触らない
	bool SetTextureParamIfEmpty(Engine::SubMeshMaterial& subMesh, const char* paramName,
		const Engine::AssetID& texture) {

		if (!texture || subMesh.parameterOverrides.count(paramName)) {
			return false;
		}
		Engine::MaterialParameterValue value{};
		value.value = texture;
		subMesh.parameterOverrides[paramName] = value;
		return true;
	}

	// サブメッシュ表示名生成
	std::string BuildSubMeshName(const aiMesh* mesh, uint32_t meshIndex, const aiMaterial* material) {

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
	Engine::Vector3 ComputeMeshLocalCenter(const aiMesh* mesh) {

		if (!mesh || mesh->mNumVertices == 0) {
			return Engine::Vector3::AnyInit(0.0f);
		}

		Engine::Vector3 minV(FLT_MAX, FLT_MAX, FLT_MAX);
		Engine::Vector3 maxV(-FLT_MAX, -FLT_MAX, -FLT_MAX);
		for (uint32_t v = 0; v < mesh->mNumVertices; ++v) {

			aiVector3D pos = mesh->mVertices[v];

			Engine::Vector3 p(-pos.x, pos.y, pos.z);

			minV.x = (std::min)(minV.x, p.x);
			minV.y = (std::min)(minV.y, p.y);
			minV.z = (std::min)(minV.z, p.z);

			maxV.x = (std::max)(maxV.x, p.x);
			maxV.y = (std::max)(maxV.y, p.y);
			maxV.z = (std::max)(maxV.z, p.z);
		}
		return (minV + maxV) * 0.5f;
	}
}

bool Engine::MeshSubMeshAuthoring::TryBuildLayout(AssetDatabase* assetDatabase,
	AssetID meshAssetID, std::vector<MeshSubMeshLayoutItem>& outLayout) {

	outLayout.clear();
	if (!assetDatabase || !meshAssetID) {
		return false;
	}

	const std::filesystem::path fullPath = assetDatabase->ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return false;
	}

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(fullPath.string(), aiProcess_Triangulate | aiProcess_SortByPType);
	if (!scene || !scene->HasMeshes()) {
		return false;
	}

	TextureAssetResolver textureResolver{};
	textureResolver.Build(fullPath);

	outLayout.reserve(scene->mNumMeshes);
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!mesh || mesh->mNumVertices == 0 || mesh->mNumFaces == 0) {
			continue;
		}

		const aiMaterial* material = (mesh->mMaterialIndex < scene->mNumMaterials) ?
			scene->mMaterials[mesh->mMaterialIndex] : nullptr;
		MeshSubMeshLayoutItem item{};
		item.sourceSubMeshIndex = meshIndex;
		item.name = BuildSubMeshName(mesh, meshIndex, material);
		item.sourcePivot = ComputeMeshLocalCenter(mesh);

		if (material && assetDatabase) {

			// FindByPathで見つからない場合はImportOrGetでメタを作成してから解決する
			auto resolveAsset = [&](const std::string& assetPath) -> AssetID {
				if (assetPath.empty()) {
					return {};
				}
				if (const auto* meta = assetDatabase->FindByPath(assetPath)) {
					return meta->guid;
				}
				return assetDatabase->ImportOrGet(assetPath, AssetType::Texture);
			};

			// AssimpMaterialTextureExtractor::ExtractはaiMaterial* (非const)を要求する
			aiMaterial* mat = const_cast<aiMaterial*>(material);

			const std::string baseColorReference = AssimpMaterialTextureExtractor::Extract(
				mat, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
			const std::string normalReference = AssimpMaterialTextureExtractor::Extract(
				mat, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT });

			item.defaultTextureAssets.baseColorTexture = resolveAsset(textureResolver.ResolveAssetPath(baseColorReference));
			item.defaultTextureAssets.normalTexture =
				resolveAsset(textureResolver.ResolveNormalAssetPath(normalReference, baseColorReference));
			item.defaultTextureAssets.metallicRoughnessTexture = resolveAsset(textureResolver.ResolveAssetPath(
				AssimpMaterialTextureExtractor::Extract(mat, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_UNKNOWN })));
			item.defaultTextureAssets.specularTexture = resolveAsset(textureResolver.ResolveAssetPath(
				AssimpMaterialTextureExtractor::Extract(mat, { aiTextureType_SPECULAR })));
			item.defaultTextureAssets.emissiveTexture = resolveAsset(textureResolver.ResolveAssetPath(
				AssimpMaterialTextureExtractor::Extract(mat, { aiTextureType_EMISSIVE, aiTextureType_EMISSION_COLOR })));
			item.defaultTextureAssets.occlusionTexture = resolveAsset(textureResolver.ResolveAssetPath(
				AssimpMaterialTextureExtractor::Extract(mat, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP })));

			// マテリアル係数を読む、PBRのBASE_COLORが無ければmtl系のCOLOR_DIFFUSEへフォールバックする
			aiColor4D baseColor{};
			if (material->Get(AI_MATKEY_BASE_COLOR, baseColor) == AI_SUCCESS) {

				item.baseColorFactor = Color4(baseColor.r, baseColor.g, baseColor.b, baseColor.a);
				item.hasBaseColorFactor = true;
			} else {

				aiColor3D diffuse{};
				if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
					item.baseColorFactor = Color4(diffuse.r, diffuse.g, diffuse.b, 1.0f);
					item.hasBaseColorFactor = true;
				}
			}
			aiColor3D emissive{};
			if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS) {
				item.emissiveFactor = Color4(emissive.r, emissive.g, emissive.b, 1.0f);
				item.hasEmissiveFactor = true;
			}
			ai_real metallic = 0.0f;
			if (material->Get(AI_MATKEY_METALLIC_FACTOR, metallic) == AI_SUCCESS) {
				item.metallicFactor = static_cast<float>(metallic);
				item.hasMetallicFactor = true;
			}
			ai_real roughness = 1.0f;
			if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
				item.roughnessFactor = static_cast<float>(roughness);
				item.hasRoughnessFactor = true;
			}
		}

		outLayout.emplace_back(std::move(item));
	}
	return true;
}

bool Engine::MeshSubMeshAuthoring::SyncComponentToLayout(
	const std::vector<MeshSubMeshLayoutItem>& layout,
	MeshRendererComponent& renderer, bool preserveOverrides) {

	// 空レイアウトなら空に揃える
	if (layout.empty()) {
		if (renderer.subMeshes.empty()) {
			return false;
		}
		renderer.subMeshes.clear();
		return true;
	}

	// すでに一致しているなら何もしない
	bool alreadyMatched = (renderer.subMeshes.size() == layout.size());
	if (alreadyMatched) {

		bool updated = false;
		for (size_t i = 0; i < layout.size(); ++i) {

			auto& current = renderer.subMeshes[i];
			if (current.name != layout[i].name ||
				current.sourceSubMeshIndex != layout[i].sourceSubMeshIndex ||
				!current.stableID) {
				alreadyMatched = false;
				break;
			}
			if (current.sourcePivot.x != layout[i].sourcePivot.x ||
				current.sourcePivot.y != layout[i].sourcePivot.y ||
				current.sourcePivot.z != layout[i].sourcePivot.z) {

				current.sourcePivot = layout[i].sourcePivot;
				updated = true;
			}
			// preserveOverrides=trueのとき、空スロットもユーザーの明示的な削除として扱い上書きしない
			if (!preserveOverrides) {

				const auto& def = layout[i].defaultTextureAssets;
				updated |= SetTextureParamIfEmpty(current, "baseColorTexture", def.baseColorTexture);
				updated |= SetTextureParamIfEmpty(current, "normalTexture", def.normalTexture);
				updated |= SetTextureParamIfEmpty(current, "emissiveTexture", def.emissiveTexture);
			}
		}
		if (alreadyMatched) {
			return updated;
		}
	}

	const std::vector<SubMeshMaterial> oldSubMeshes = renderer.subMeshes;
	std::vector<bool> used(oldSubMeshes.size(), false);
	auto findReusableOldIndex = [&](size_t newIndex, const MeshSubMeshLayoutItem& item) -> int32_t {

		if (!preserveOverrides) {
			return -1;
		}
		for (size_t oldIndex = 0; oldIndex < oldSubMeshes.size(); ++oldIndex) {
			if (used[oldIndex]) {
				continue;
			}
			if (oldSubMeshes[oldIndex].sourceSubMeshIndex == item.sourceSubMeshIndex) {
				return static_cast<int32_t>(oldIndex);
			}
		}
		for (size_t oldIndex = 0; oldIndex < oldSubMeshes.size(); ++oldIndex) {
			if (used[oldIndex]) {
				continue;
			}
			if (oldSubMeshes[oldIndex].name == item.name) {
				return static_cast<int32_t>(oldIndex);
			}
		}
		// 同位置フォールバック
		if (newIndex < oldSubMeshes.size() && !used[newIndex]) {
			return static_cast<int32_t>(newIndex);
		}
		return -1;
		};

	// レイアウトに合わせてサブメッシュを再構築する
	std::vector<SubMeshMaterial> rebuilt{};
	rebuilt.resize(layout.size());
	for (size_t i = 0; i < layout.size(); ++i) {

		SubMeshMaterial entry{};

		const int32_t reusableOldIndex = findReusableOldIndex(i, layout[i]);
		if (0 <= reusableOldIndex) {
			entry = oldSubMeshes[reusableOldIndex];
			used[reusableOldIndex] = true;
			// preserveOverrides=trueのとき、空スロットもユーザーの明示的な削除として扱い上書きしない
			if (!preserveOverrides) {

				const auto& def = layout[i].defaultTextureAssets;
				SetTextureParamIfEmpty(entry, "baseColorTexture", def.baseColorTexture);
				SetTextureParamIfEmpty(entry, "normalTexture", def.normalTexture);
				SetTextureParamIfEmpty(entry, "emissiveTexture", def.emissiveTexture);
			}
		} else {
			// 新規エントリはモデルのデフォルトテクスチャで初期化
			const auto& defaults = layout[i].defaultTextureAssets;
			SetTextureParamIfEmpty(entry, "baseColorTexture", defaults.baseColorTexture);
			SetTextureParamIfEmpty(entry, "normalTexture", defaults.normalTexture);
			SetTextureParamIfEmpty(entry, "emissiveTexture", defaults.emissiveTexture);
		}

		// 正規レイアウトを上書き
		entry.name = layout[i].name;
		entry.sourceSubMeshIndex = layout[i].sourceSubMeshIndex;
		entry.sourcePivot = layout[i].sourcePivot;

		// 実Entity化、選択保持のための永続ID
		if (!entry.stableID) {

			entry.stableID = UUID::New();
		}
		rebuilt[i] = std::move(entry);
	}
	renderer.subMeshes = std::move(rebuilt);
	return true;
}

bool Engine::MeshSubMeshAuthoring::SyncComponent(AssetDatabase* assetDatabase,
	MeshRendererComponent& renderer, bool preserveOverrides) {

	if (!renderer.mesh) {
		if (renderer.subMeshes.empty()) {
			return false;
		}
		renderer.subMeshes.clear();
		return true;
	}

	std::vector<MeshSubMeshLayoutItem> layout{};
	// レイアウトが解決できない時は、今の内容を壊さない
	if (!TryBuildLayout(assetDatabase, renderer.mesh, layout)) {
		return false;
	}
	return SyncComponentToLayout(layout, renderer, preserveOverrides);
}

int32_t Engine::MeshSubMeshAuthoring::FindSubMeshIndexByStableID(
	const MeshRendererComponent& renderer, UUID stableID) {

	if (!stableID) {
		return -1;
	}
	for (uint32_t i = 0; i < static_cast<uint32_t>(renderer.subMeshes.size()); ++i) {
		if (renderer.subMeshes[i].stableID == stableID) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}
