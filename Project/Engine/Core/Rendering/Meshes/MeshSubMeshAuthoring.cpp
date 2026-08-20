#include "MeshSubMeshAuthoring.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportUtility.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <mutex>
#include <string_view>
#include <unordered_map>

//============================================================================
//	MeshSubMeshAuthoring classMethods
//============================================================================
namespace {

	struct CachedMeshLayout {

		uint64_t databaseRevision = 0;
		std::vector<Engine::MeshSubMeshLayoutItem> layout{};
		Engine::MeshAssetAuthoringInfo info{};
	};

	std::mutex gLayoutCacheMutex;
	std::unordered_map<const Engine::AssetDatabase*,
		std::unordered_map<Engine::AssetID, CachedMeshLayout>> gLayoutCaches;

	// モデルのマテリアル係数とテクスチャをmaterialInstanceへ流す、overwrite=falseは未設定のみ
	bool ApplyLayoutItemToSubMesh(Engine::SubMeshMaterial& subMesh,
		const Engine::MeshSubMeshLayoutItem& item, bool overwrite) {

		bool changed = false;
		auto setParam = [&](std::string_view name,
			const Engine::MaterialParameterValue& value) {

			const Engine::MaterialParameterID id =
				Engine::MaterialParameterID::FromName(name);
			if (!overwrite &&
				subMesh.materialInstance.Find(id)) {
				return;
			}
			subMesh.materialInstance.Set(
				id, name,
				Engine::ResolveMaterialParameterSemantic(name),
				value);
			changed = true;
			};
		auto setTexture = [&](std::string_view name,
			const Engine::AssetID& texture) {
			if (!texture) {
				return;
			}
			Engine::MaterialParameterValue value{};
			value.value = texture;
			setParam(name, value);
			};
		auto colorValue = [](const Engine::Color4& c) {
			Engine::MaterialParameterValue v{}; v.value = c; return v;
			};
		auto floatValue = [](float f) {
			Engine::MaterialParameterValue v{}; v.value = f; return v;
			};

		if (item.hasBaseColorFactor) { setParam(Engine::MaterialParameterNames::BaseColor, colorValue(item.baseColorFactor)); }
		if (item.hasEmissiveFactor) { setParam(Engine::MaterialParameterNames::EmissiveColor, colorValue(item.emissiveFactor)); }
		if (item.hasMetallicFactor) { setParam(Engine::MaterialParameterNames::Metallic, floatValue(item.metallicFactor)); }
		if (item.hasRoughnessFactor) { setParam(Engine::MaterialParameterNames::Roughness, floatValue(item.roughnessFactor)); }

		const auto& tex = item.defaultTextureAssets;
		setTexture(Engine::MaterialParameterNames::BaseColorTexture, tex.baseColorTexture);
		setTexture(Engine::MaterialParameterNames::NormalTexture, tex.normalTexture);
		setTexture(Engine::MaterialParameterNames::EmissiveTexture, tex.emissiveTexture);
		setTexture(Engine::MaterialParameterNames::MetallicRoughnessTexture, tex.metallicRoughnessTexture);
		setTexture(Engine::MaterialParameterNames::MetallicTexture, tex.metallicTexture);
		setTexture(Engine::MaterialParameterNames::RoughnessTexture, tex.roughnessTexture);
		setTexture(Engine::MaterialParameterNames::DisplacementTexture, tex.displacementTexture);
		setTexture(Engine::MaterialParameterNames::AmbientOcclusionTexture, tex.occlusionTexture);
		setTexture(Engine::MaterialParameterNames::SpecularTexture, tex.specularTexture);
		return changed;
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
	AssetID meshAssetID, std::vector<MeshSubMeshLayoutItem>& outLayout,
	MeshAssetAuthoringInfo* outInfo) {

	outLayout.clear();
	MeshAssetAuthoringInfo resolvedInfo{};
	if (outInfo) {
		*outInfo = {};
	}
	if (!assetDatabase || !meshAssetID) {
		return false;
	}

	const uint64_t databaseRevision =
		assetDatabase->GetStructureRevision();
	{
		std::scoped_lock lock(gLayoutCacheMutex);
		const auto databaseCache = gLayoutCaches.find(assetDatabase);
		if (databaseCache != gLayoutCaches.end()) {

			const auto cached = databaseCache->second.find(meshAssetID);
			if (cached != databaseCache->second.end() &&
				cached->second.databaseRevision == databaseRevision) {

				outLayout = cached->second.layout;
				if (outInfo) {
					*outInfo = cached->second.info;
				}
				return true;
			}
		}
	}

	const std::filesystem::path fullPath = assetDatabase->ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return false;
	}

	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(
		Algorithm::PathToUTF8(fullPath),
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType);
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
		if (mesh->HasBones()) {
			resolvedInfo.hasBones = true;
		}

		const aiMaterial* material = (mesh->mMaterialIndex < scene->mNumMaterials) ?
			scene->mMaterials[mesh->mMaterialIndex] : nullptr;
		MeshSubMeshLayoutItem item{};
		item.sourceSubMeshIndex = meshIndex;
		item.vertexCount = mesh->mNumVertices;
		item.name = Engine::MeshImportUtility::BuildSubMeshName(mesh, meshIndex, material);
		item.sourcePivot = ComputeMeshLocalCenter(mesh);

		if (material && assetDatabase) {
			const MeshImportUtility::ImportedMaterialSurface surface =
				MeshImportUtility::ReadMaterialSurface(material);
			item.sourceSurfaceMode = surface.surfaceMode;
			item.alphaCutoff = surface.alphaCutoff;

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
				mat, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA });
			const std::string heightReference =
				AssimpMaterialTextureExtractor::Extract(
					mat, { aiTextureType_HEIGHT });
			AssimpMaterialTextureExtractor::PBRTextureReferences pbrTextures =
				AssimpMaterialTextureExtractor::ExtractPBR(mat);
			const std::string normalOrHeightReference =
				normalReference.empty() ? heightReference : normalReference;
			if (pbrTextures.displacement.empty() &&
				!normalReference.empty()) {

				pbrTextures.displacement = heightReference;
			}

			item.defaultTextureAssets.baseColorTexture = resolveAsset(textureResolver.ResolveAssetPath(baseColorReference));
			item.defaultTextureAssets.normalTexture =
				resolveAsset(textureResolver.ResolveNormalAssetPath(
					normalOrHeightReference, baseColorReference));
			item.defaultTextureAssets.metallicRoughnessTexture =
				resolveAsset(textureResolver.ResolveAssetPath(pbrTextures.metallicRoughness));
			item.defaultTextureAssets.metallicTexture =
				resolveAsset(textureResolver.ResolveAssetPath(pbrTextures.metallic));
			item.defaultTextureAssets.roughnessTexture =
				resolveAsset(textureResolver.ResolveAssetPath(pbrTextures.roughness));
			item.defaultTextureAssets.displacementTexture =
				resolveAsset(textureResolver.ResolveAssetPath(pbrTextures.displacement));
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
	if (outInfo) {
		*outInfo = resolvedInfo;
	}
	{
		std::scoped_lock lock(gLayoutCacheMutex);
		CachedMeshLayout& cached =
			gLayoutCaches[assetDatabase][meshAssetID];
		cached.databaseRevision =
			assetDatabase->GetStructureRevision();
		cached.layout = outLayout;
		cached.info = resolvedInfo;
	}
	return true;
}

void Engine::MeshSubMeshAuthoring::InvalidateCachedLayout(
	AssetID meshAssetID) {

	if (!meshAssetID) {
		return;
	}
	std::scoped_lock lock(gLayoutCacheMutex);
	for (auto& [database, cache] : gLayoutCaches) {
		(void)database;
		cache.erase(meshAssetID);
	}
}

bool Engine::MeshSubMeshAuthoring::SyncComponentToLayout(
	const std::vector<MeshSubMeshLayoutItem>& layout,
	std::vector<SubMeshMaterial>& subMeshes, bool preserveOverrides) {

	// 空レイアウトなら空に揃える
	if (layout.empty()) {
		if (subMeshes.empty()) {
			return false;
		}
		subMeshes.clear();
		return true;
	}

	// すでに一致しているなら何もしない
	bool alreadyMatched = (subMeshes.size() == layout.size());
	if (alreadyMatched) {

		bool updated = false;
		for (size_t i = 0; i < layout.size(); ++i) {

			auto& current = subMeshes[i];
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
			const bool sourceSurfaceWasUnset =
				current.sourceSurfaceMode == MaterialSurfaceMode::Auto;
			if (current.sourceSurfaceMode != layout[i].sourceSurfaceMode) {
				current.sourceSurfaceMode = layout[i].sourceSurfaceMode;
				updated = true;
			}
			if ((!preserveOverrides || sourceSurfaceWasUnset) &&
				current.alphaCutoff != layout[i].alphaCutoff) {
				current.alphaCutoff = layout[i].alphaCutoff;
				updated = true;
			}
			// preserveOverrides=trueのとき、空スロットもユーザーの明示的な削除として扱い上書きしない
			if (!preserveOverrides) {
				updated |= ApplyLayoutItemToSubMesh(current, layout[i], false);
			}
		}
		if (alreadyMatched) {
			return updated;
		}
	}

	const std::vector<SubMeshMaterial> oldSubMeshes = subMeshes;
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
		const bool reused = 0 <= reusableOldIndex;
		if (reused) {
			entry = oldSubMeshes[reusableOldIndex];
			used[reusableOldIndex] = true;
			// preserveOverrides=trueのとき、空スロットもユーザーの明示的な削除として扱い上書きしない
			if (!preserveOverrides) {
				ApplyLayoutItemToSubMesh(entry, layout[i], false);
			}
		} else {
			// 新規エントリはモデルの係数とデフォルトテクスチャで初期化
			ApplyLayoutItemToSubMesh(entry, layout[i], false);
		}

		// 正規レイアウトを上書き
		entry.name = layout[i].name;
		entry.sourceSubMeshIndex = layout[i].sourceSubMeshIndex;
		entry.sourcePivot = layout[i].sourcePivot;
		const bool sourceSurfaceWasUnset =
			entry.sourceSurfaceMode == MaterialSurfaceMode::Auto;
		entry.sourceSurfaceMode = layout[i].sourceSurfaceMode;
		if (!reused || !preserveOverrides || sourceSurfaceWasUnset) {
			entry.alphaCutoff = layout[i].alphaCutoff;
		}

		// 実Entity化、選択保持のための永続ID
		if (!entry.stableID) {

			entry.stableID = UUID::New();
		}
		rebuilt[i] = std::move(entry);
	}
	subMeshes = std::move(rebuilt);
	return true;
}

bool Engine::MeshSubMeshAuthoring::SyncComponent(AssetDatabase* assetDatabase,
	AssetID meshAssetID, std::vector<SubMeshMaterial>& subMeshes,
	bool preserveOverrides) {

	if (!meshAssetID) {
		if (subMeshes.empty()) {
			return false;
		}
		subMeshes.clear();
		return true;
	}

	std::vector<MeshSubMeshLayoutItem> layout{};
	// レイアウトが解決できない時は、今の内容を壊さない
	if (!TryBuildLayout(assetDatabase, meshAssetID, layout)) {
		return false;
	}
	return SyncComponentToLayout(layout, subMeshes, preserveOverrides);
}

bool Engine::MeshSubMeshAuthoring::SyncEntity(AssetDatabase* assetDatabase,
	ECSWorld& world, const Entity& entity, bool preserveOverrides) {

	MeshRendererComponent* renderer =
		world.TryGetComponent<MeshRendererComponent>(entity);
	if (!renderer) {
		return false;
	}
	const std::span<const SubMeshMaterial> source =
		GetMeshSubMeshes(world, entity);
	std::vector<SubMeshMaterial> subMeshes(source.begin(), source.end());
	if (!SyncComponent(assetDatabase, renderer->mesh, subMeshes, preserveOverrides)) {
		return false;
	}
	SetMeshSubMeshes(world, entity, subMeshes);
	return true;
}

void Engine::MeshSubMeshAuthoring::ApplyModelMaterialParameters(
	const std::vector<MeshSubMeshLayoutItem>& layout,
	std::span<SubMeshMaterial> subMeshes) {

	for (SubMeshMaterial& subMesh : subMeshes) {

		// 元のサブメッシュインデックスでモデル側の対応itemを探す
		const MeshSubMeshLayoutItem* item = nullptr;
		for (const MeshSubMeshLayoutItem& layoutItem : layout) {
			if (layoutItem.sourceSubMeshIndex == subMesh.sourceSubMeshIndex) {
				item = &layoutItem;
				break;
			}
		}
		if (item) {
			ApplyLayoutItemToSubMesh(subMesh, *item, true);
		}
	}
}

int32_t Engine::MeshSubMeshAuthoring::FindSubMeshIndexByStableID(
	std::span<const SubMeshMaterial> subMeshes, UUID stableID) {

	if (!stableID) {
		return -1;
	}
	for (uint32_t i = 0; i < static_cast<uint32_t>(subMeshes.size()); ++i) {
		if (subMeshes[i].stableID == stableID) {
			return static_cast<int32_t>(i);
		}
	}
	return -1;
}
