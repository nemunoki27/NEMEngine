#include "MeshSubMeshAuthoring.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/Rendering/Textures/TextureAssetResolver.h>
#include <Engine/Core/Rendering/Meshes/Import/FBXDocumentReferences.h>
#include <Engine/Core/Rendering/Meshes/Import/AssimpMaterialTextureExtractor.h>
#include <Engine/Core/Rendering/Meshes/Import/MeshImportUtility.h>
#include <Engine/Core/Rendering/Meshes/Import/ModelFileIOSystem.h>
#include <Engine/Core/Rendering/Meshes/Import/FBXScenePreparation.h>

// c++
#include <algorithm>
#include <map>
#include <mutex>
#include <unordered_map>
// assimp
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

namespace {

	struct CachedMeshLayout {

		uint64_t databaseRevision = 0;
		uint64_t contentRevision = 0;
		std::vector<Engine::MeshSubMeshLayoutItem> layout{};
		Engine::MeshAssetAuthoringInfo info{};
	};

	std::mutex gLayoutCacheMutex;
	using CacheLifetime = std::weak_ptr<const uint8_t>;
	std::map<CacheLifetime, std::unordered_map<Engine::AssetID, CachedMeshLayout>,
		std::owner_less<CacheLifetime>> gLayoutCaches;

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

	const std::filesystem::path fullPath = assetDatabase->ResolveFullPath(meshAssetID);
	if (fullPath.empty() || !std::filesystem::exists(fullPath)) {
		return false;
	}
	const auto cacheKey = assetDatabase->GetCacheLifetime();
	const uint64_t databaseRevision = assetDatabase->GetStructureRevision();
	const uint64_t contentRevision = assetDatabase->GetContentRevision(meshAssetID);
	{
		std::scoped_lock lock(gLayoutCacheMutex);
		// 終了したProjectや置換前の索引の結果を解放する
		std::erase_if(gLayoutCaches, [](const auto& entry) { return entry.first.expired(); });
		const auto databaseCache = gLayoutCaches.find(cacheKey);
		if (databaseCache != gLayoutCaches.end()) {

			const auto cached = databaseCache->second.find(meshAssetID);
			if (cached != databaseCache->second.end() &&
				cached->second.databaseRevision == databaseRevision && cached->second.contentRevision == contentRevision) {

				outLayout = cached->second.layout;
				if (outInfo) {
					*outInfo = cached->second.info;
				}
				return true;
			}
		}
	}

	Assimp::Importer importer;
	auto* fileSystem = new ModelFileIOSystem(fullPath);
	importer.SetIOHandler(fileSystem);
	const aiScene* scene = importer.ReadFile(
		fileSystem->GetModelPath(),
		aiProcess_Triangulate |
		aiProcess_JoinIdenticalVertices |
		aiProcess_SortByPType);
	scene = FBXScenePreparation::Prepare(importer, scene, fullPath);
	if (!scene || !scene->HasMeshes()) {
		return false;
	}

	TextureAssetResolver textureResolver{};
	textureResolver.Build(fullPath);

	outLayout.reserve(scene->mNumMeshes);
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

		const aiMesh* mesh = scene->mMeshes[meshIndex];
		if (!MeshImportUtility::HasTriangleGeometry(mesh)) {
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
				MeshImportUtility::ReadMaterialSurface(material,
					FBXDocumentReferences::IsDocumentPath(fullPath) ? &textureResolver : nullptr);
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

			// 描画側と同じTexture候補をAssetへ登録する
			const auto textures = AssimpMaterialTextureExtractor::ExtractResolved(material, textureResolver);
			item.defaultTextureAssets.baseColorTexture = resolveAsset(textures.baseColorTexturePath);
			item.defaultTextureAssets.normalTexture = resolveAsset(textures.normalTexturePath);
			item.defaultTextureAssets.metallicRoughnessTexture = resolveAsset(textures.metallicRoughnessTexturePath);
			item.defaultTextureAssets.metallicTexture = resolveAsset(textures.metallicTexturePath);
			item.defaultTextureAssets.roughnessTexture = resolveAsset(textures.roughnessTexturePath);
			item.defaultTextureAssets.displacementTexture = resolveAsset(textures.displacementTexturePath);
			item.defaultTextureAssets.specularTexture = resolveAsset(textures.specularTexturePath);
			item.defaultTextureAssets.emissiveTexture = resolveAsset(textures.emissiveTexturePath);
			item.defaultTextureAssets.occlusionTexture = resolveAsset(textures.occlusionTexturePath);
			item.defaultTextureAssets.opacityTexture = resolveAsset(textures.opacityTexturePath);

			// 通常描画と同じ係数を初期値へ渡す
			const auto factors = MeshImportUtility::ReadMaterialFactors(material);
			item.baseColorFactor = factors.baseColor;
			item.emissiveFactor = factors.emissive;
			item.metallicFactor = factors.metallic;
			item.roughnessFactor = factors.roughness;
			item.hasBaseColorFactor = factors.hasBaseColor;
			item.hasEmissiveFactor = factors.hasEmissive;
			item.hasMetallicFactor = factors.hasMetallic;
			item.hasRoughnessFactor = factors.hasRoughness;
		}

		outLayout.emplace_back(std::move(item));
	}
	// 描画できないモデルで既存の編集値を置き換えない
	if (outLayout.empty()) {
		return false;
	}
	if (outInfo) {
		*outInfo = resolvedInfo;
	}
	// コピーが完了してから旧キャッシュを置き換える
	CachedMeshLayout cached{ assetDatabase->GetStructureRevision(), contentRevision, outLayout, resolvedInfo };
	{
		std::scoped_lock lock(gLayoutCacheMutex);
		gLayoutCaches[cacheKey].insert_or_assign(meshAssetID, std::move(cached));
	}
	return true;
}

void Engine::MeshSubMeshAuthoring::InvalidateCachedLayout(
	AssetID meshAssetID) {

	if (!meshAssetID) {
		return;
	}
	std::scoped_lock lock(gLayoutCacheMutex);
	std::erase_if(gLayoutCaches, [](const auto& entry) { return entry.first.expired(); });
	for (auto& [database, cache] : gLayoutCaches) {
		cache.erase(meshAssetID);
	}
}

