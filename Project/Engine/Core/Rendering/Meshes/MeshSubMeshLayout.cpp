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

	struct SourceLayoutItem {

		Engine::MeshSubMeshLayoutItem layout;
		Engine::ImportedMeshTextureSet textures;
	};
	struct SourceLayout {

		std::filesystem::path path;
		uint64_t contentRevision = 0;
		std::filesystem::file_time_type writeTime;
		uintmax_t fileSize = 0;
		std::vector<SourceLayoutItem> items;
		Engine::MeshAssetAuthoringInfo info;
	};
	struct CachedMeshLayout {

		uint64_t databaseRevision = 0;
		uint64_t contentRevision = 0;
		std::vector<Engine::MeshSubMeshLayoutItem> layout{};
		Engine::MeshAssetAuthoringInfo info{};
		std::shared_ptr<const SourceLayout> source;
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

	std::shared_ptr<const SourceLayout> ReadSourceLayout(const std::filesystem::path& path, uint64_t contentRevision) {

		using namespace Engine;
		const auto writeTime = std::filesystem::last_write_time(path);
		const auto fileSize = std::filesystem::file_size(path);
		Assimp::Importer importer;
		auto* fileSystem = new ModelFileIOSystem(path);
		importer.SetIOHandler(fileSystem);
		const aiScene* scene = importer.ReadFile(fileSystem->GetModelPath(),
			aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType);
		scene = FBXScenePreparation::Prepare(importer, scene, path);
		if (!scene || !scene->HasMeshes()) return {};
		TextureAssetResolver textureResolver;
		textureResolver.Build(path);
		auto source = std::make_shared<SourceLayout>();
		source->path = path;
		source->contentRevision = contentRevision;
		source->writeTime = writeTime;
		source->fileSize = fileSize;
		source->items.reserve(scene->mNumMeshes);
		// 頂点とMaterialの情報だけを確定してImporterを解放する
		for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {

			const aiMesh* mesh = scene->mMeshes[meshIndex];
			if (!MeshImportUtility::HasTriangleGeometry(mesh)) continue;
			source->info.hasBones |= mesh->HasBones();
			SourceLayoutItem item;
			auto& layout = item.layout;
			const aiMaterial* material = mesh->mMaterialIndex < scene->mNumMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;
			layout.sourceSubMeshIndex = meshIndex;
			layout.vertexCount = mesh->mNumVertices;
			layout.name = MeshImportUtility::BuildSubMeshName(mesh, meshIndex, material);
			layout.sourcePivot = ComputeMeshLocalCenter(mesh);
			if (material) {

				const auto surface = MeshImportUtility::ReadMaterialSurface(material,
					FBXDocumentReferences::IsDocumentPath(path) ? &textureResolver : nullptr);
				layout.sourceSurfaceMode = surface.surfaceMode;
				layout.alphaCutoff = surface.alphaCutoff;
				item.textures = AssimpMaterialTextureExtractor::ExtractResolved(material, textureResolver);
				const auto factors = MeshImportUtility::ReadMaterialFactors(material);
				layout.baseColorFactor = factors.baseColor;
				layout.emissiveFactor = factors.emissive;
				layout.metallicFactor = factors.metallic;
				layout.roughnessFactor = factors.roughness;
				layout.hasBaseColorFactor = factors.hasBaseColor;
				layout.hasEmissiveFactor = factors.hasEmissive;
				layout.hasMetallicFactor = factors.hasMetallic;
				layout.hasRoughnessFactor = factors.hasRoughness;
			}
			source->items.push_back(std::move(item));
		}
		// 準備中に置き換わったモデルは公開しない
		if (source->items.empty() || std::filesystem::last_write_time(path) != writeTime ||
			std::filesystem::file_size(path) != fileSize) return {};
		return source;
	}
}

bool Engine::MeshSubMeshAuthoring::PrepareSourceLayout(std::weak_ptr<const uint8_t> databaseLifetime,
	AssetID meshAssetID, const std::filesystem::path& path, uint64_t contentRevision) {

	if (databaseLifetime.expired() || !meshAssetID || path.empty()) return false;
	{
		std::scoped_lock lock(gLayoutCacheMutex);
		std::erase_if(gLayoutCaches, [](const auto& entry) { return entry.first.expired(); });
		const auto database = gLayoutCaches.find(databaseLifetime);
		if (database != gLayoutCaches.end()) {
			const auto entry = database->second.find(meshAssetID);
			if (entry != database->second.end() && entry->second.source &&
				entry->second.source->path == path && entry->second.source->contentRevision == contentRevision &&
				entry->second.source->writeTime == std::filesystem::last_write_time(path) &&
				entry->second.source->fileSize == std::filesystem::file_size(path)) return true;
		}
	}
	const auto source = ReadSourceLayout(path, contentRevision);
	if (!source) return false;
	std::scoped_lock lock(gLayoutCacheMutex);
	if (databaseLifetime.expired()) return false;
	// Assetの登録前の情報を既存Cacheと一緒に保持する
	auto& entry = gLayoutCaches[databaseLifetime][meshAssetID];
	entry.source = source;
	entry.layout.clear();
	return true;
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
				!cached->second.layout.empty() && cached->second.source && cached->second.source->path == fullPath &&
				cached->second.databaseRevision == databaseRevision && cached->second.contentRevision == contentRevision) {

				outLayout = cached->second.layout;
				if (outInfo) {
					*outInfo = cached->second.info;
				}
				return true;
			}
		}
	}

	if (!PrepareSourceLayout(cacheKey, meshAssetID, fullPath, contentRevision)) return false;
	std::shared_ptr<const SourceLayout> source;
	{
		std::scoped_lock lock(gLayoutCacheMutex);
		source = gLayoutCaches.at(cacheKey).at(meshAssetID).source;
	}
	resolvedInfo = source->info;
	outLayout.reserve(source->items.size());
	// 索引への登録と参照の解決は呼出し側で行う
	const auto resolveAsset = [&](const std::string& assetPath) -> AssetID {

		if (assetPath.empty()) return {};
		if (const auto* meta = assetDatabase->FindByPath(assetPath)) return meta->guid;
		return assetDatabase->ImportOrGet(assetPath, AssetType::Texture);
	};
	for (const auto& prepared : source->items) {

		MeshSubMeshLayoutItem item = prepared.layout;
		const auto& textures = prepared.textures;
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
		outLayout.push_back(std::move(item));
	}
	// 描画できないモデルで既存の編集値を置き換えない
	if (outLayout.empty()) {
		return false;
	}
	if (outInfo) {
		*outInfo = resolvedInfo;
	}
	// コピーが完了してから旧キャッシュを置き換える
	CachedMeshLayout cached{ assetDatabase->GetStructureRevision(), contentRevision, outLayout, resolvedInfo, source };
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

