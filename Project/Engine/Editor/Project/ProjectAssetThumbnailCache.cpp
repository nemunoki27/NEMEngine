#include "ProjectAssetThumbnailCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Graphics/Texture/TextureUploadService.h>
#include <Engine/Core/Graphics/Texture/GPUTextureResource.h>

//============================================================================
//	ProjectAssetThumbnailCache classMethods
//============================================================================

void Engine::ProjectAssetThumbnailCache::Init(TextureUploadService& textureUploadService) {

	// すでに初期化されている場合は何もしない
	if (initialized_) {
		return;
	}

	textureUploadService_ = &textureUploadService;

	// デフォルトで表示するアイコンを作成する
	CreateDefaultIcons();
	initialized_ = true;
}

void Engine::ProjectAssetThumbnailCache::Finalize() {

	defaultIcons_.clear();
	folderIconKey_.clear();
	textureUploadService_ = nullptr;
	initialized_ = false;
}

void Engine::ProjectAssetThumbnailCache::CreateDefaultIcons() {

	if (!textureUploadService_) {
		return;
	}

	const std::string baseKey = "Engine/Assets/Textures/Engine/Editor/File/";
	folderIconKey_ = "folder.dds";
	textureUploadService_->RequestTextureFile(folderIconKey_, baseKey + folderIconKey_);

	defaultIcons_[AssetType::Scene].textureKey = "scene.dds";
	defaultIcons_[AssetType::Scene].assetPath = baseKey + "scene.dds";
	defaultIcons_[AssetType::Prefab].textureKey = "prefab.dds";
	defaultIcons_[AssetType::Prefab].assetPath = baseKey + "prefab.dds";
	defaultIcons_[AssetType::Material].textureKey = "material.dds";
	defaultIcons_[AssetType::Material].assetPath = baseKey + "material.dds";
	defaultIcons_[AssetType::Shader].textureKey = "shader.dds";
	defaultIcons_[AssetType::Shader].assetPath = baseKey + "shader.dds";
	defaultIcons_[AssetType::Texture].textureKey = "texture.dds";
	defaultIcons_[AssetType::Texture].assetPath = baseKey + "texture.dds";
	defaultIcons_[AssetType::Mesh].textureKey = "mesh.dds";
	defaultIcons_[AssetType::Mesh].assetPath = baseKey + "mesh.dds";
	defaultIcons_[AssetType::RenderPipeline].textureKey = "renderPipeline.dds";
	defaultIcons_[AssetType::RenderPipeline].assetPath = baseKey + "renderPipeline.dds";
	defaultIcons_[AssetType::Font].textureKey = "font.dds";
	defaultIcons_[AssetType::Font].assetPath = baseKey + "font.dds";
	defaultIcons_[AssetType::Script].textureKey = "shader.dds";
	defaultIcons_[AssetType::Script].assetPath = baseKey + "shader.dds";
	defaultIcons_[AssetType::Audio].textureKey = "unknown.dds";
	defaultIcons_[AssetType::Audio].assetPath = baseKey + "unknown.dds";
	defaultIcons_[AssetType::Unknown].textureKey = "unknown.dds";
	defaultIcons_[AssetType::Unknown].assetPath = baseKey + "unknown.dds";

	for (const auto& [type, icon] : defaultIcons_) {

		textureUploadService_->RequestTextureFile(icon.textureKey, icon.assetPath);
	}
}

std::string Engine::ProjectAssetThumbnailCache::MakeThumbnailKey(const std::string& assetPath) const {

	return "editor:project:thumbnail:" + assetPath;
}

ImTextureID Engine::ProjectAssetThumbnailCache::TryGetTextureID(const std::string& key) const {

	if (!textureUploadService_ || key.empty()) {
		return ImTextureID{};
	}

	if (const auto* texture = textureUploadService_->GetTexture(key)) {
		return static_cast<ImTextureID>(texture->gpuHandle.ptr);
	}

	return ImTextureID{};
}

ImTextureID Engine::ProjectAssetThumbnailCache::GetFolderIconTextureID() const {

	return TryGetTextureID(folderIconKey_);
}

ImTextureID Engine::ProjectAssetThumbnailCache::GetDefaultTypeIcon(AssetType type) const {

	auto it = defaultIcons_.find(type);
	if (it != defaultIcons_.end()) {
		ImTextureID id = TryGetTextureID(it->second.textureKey);
		if (id != ImTextureID{}) {
			return id;
		}
	}
	auto unknownIt = defaultIcons_.find(AssetType::Unknown);
	return (unknownIt != defaultIcons_.end()) ? TryGetTextureID(unknownIt->second.textureKey) : ImTextureID{};
}

ImTextureID Engine::ProjectAssetThumbnailCache::GetAssetTextureID(const std::string& assetPath, AssetType type) {

	if (!initialized_ || !textureUploadService_) {
		return ImTextureID{};
	}

	if (type != AssetType::Texture) {
		return GetDefaultTypeIcon(type);
	}

	// サムネイルのキャッシュキーを作成する
	std::string thumbnailKey = MakeThumbnailKey(assetPath);

	// すでに読みこみ済みなら実テクスチャを返す
	if (ImTextureID readyID = TryGetTextureID(thumbnailKey); readyID != ImTextureID{}) {
		return readyID;
	}

	auto state = textureUploadService_->GetState(thumbnailKey);

	// まだ一度も要求していなければ要求
	if (state == TextureRequestState::None) {

		// サムネイルのアップロードを要求する
		TextureFileRequestDesc desc{};
		desc.key = thumbnailKey;
		desc.assetPath = assetPath;
		desc.forceSRGB = true;
		textureUploadService_->RequestTextureFile(desc);
	}
	// フォールバックアイコン
	return GetDefaultTypeIcon(type);
}
