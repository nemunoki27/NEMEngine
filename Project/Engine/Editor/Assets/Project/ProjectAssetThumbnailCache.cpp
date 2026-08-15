#include "ProjectAssetThumbnailCache.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Textures/TextureUploadService.h>
#include <Engine/Core/Rendering/Textures/GPUTextureResource.h>
#include <Engine/Editor/Utility/EditorTextureHelper.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>

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
	customExtensionIcons_.clear();
	folderIconKey_.clear();
	textureUploadService_ = nullptr;
	initialized_ = false;
}

void Engine::ProjectAssetThumbnailCache::CreateDefaultIcons() {

	if (!textureUploadService_) {
		return;
	}

	folderIconKey_ = "folder.png";
	textureUploadService_->RequestTextureFile(folderIconKey_, EditorTextureHelper::MakeEditorTexturePath("File", folderIconKey_));

	defaultIcons_[AssetType::Scene].textureKey = "scene.png";
	defaultIcons_[AssetType::Scene].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "scene.png");
	defaultIcons_[AssetType::Prefab].textureKey = "prefab.png";
	defaultIcons_[AssetType::Prefab].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "prefab.png");
	defaultIcons_[AssetType::Material].textureKey = "material.png";
	defaultIcons_[AssetType::Material].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "material.png");
	defaultIcons_[AssetType::ShaderGraph].textureKey = "material.png";
	defaultIcons_[AssetType::ShaderGraph].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "material.png");
	defaultIcons_[AssetType::Shader].textureKey = "writeFile.dds";
	defaultIcons_[AssetType::Shader].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "writeFile.dds");
	defaultIcons_[AssetType::Texture].textureKey = "texture.dds";
	defaultIcons_[AssetType::Texture].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "texture.dds");
	defaultIcons_[AssetType::Mesh].textureKey = "mesh.dds";
	defaultIcons_[AssetType::Mesh].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "mesh.dds");
	defaultIcons_[AssetType::RenderPipeline].textureKey = "renderPipeline.dds";
	defaultIcons_[AssetType::RenderPipeline].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "renderPipeline.dds");
	defaultIcons_[AssetType::Font].textureKey = "font.dds";
	defaultIcons_[AssetType::Font].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "font.dds");
	defaultIcons_[AssetType::Script].textureKey = "writeFile.dds";
	defaultIcons_[AssetType::Script].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "writeFile.dds");
	defaultIcons_[AssetType::Audio].textureKey = "audio.dds";
	defaultIcons_[AssetType::Audio].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "audio.dds");
	defaultIcons_[AssetType::AnimationClip].textureKey = "animationClip.dds";
	defaultIcons_[AssetType::AnimationClip].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "animationClip.dds");
	defaultIcons_[AssetType::ParticleEffect].textureKey = "particleEffect.png";
	defaultIcons_[AssetType::ParticleEffect].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "particleEffect.png");
	defaultIcons_[AssetType::Unknown].textureKey = "unknown.dds";
	defaultIcons_[AssetType::Unknown].assetPath = EditorTextureHelper::MakeEditorTexturePath("File", "unknown.dds");
	// 設定アセットは型の有無にかかわらず同じ歯車アイコンへ揃える
	const IconEntry configIcon{
		"exeConfig.png", EditorTextureHelper::MakeEditorTexturePath("File", "exeConfig.png") };
	defaultIcons_[AssetType::RenderFeatureProfile] = configIcon;
	customExtensionIcons_[".renderfeatureprofile.json"] = configIcon;
	customExtensionIcons_[".execonfig.json"] = configIcon;
	customExtensionIcons_[".materialsettings.json"] = configIcon;

	for (const auto& [type, icon] : defaultIcons_) {

		textureUploadService_->RequestTextureFile(icon.textureKey, icon.assetPath);
	}

	for (const auto& [ext, icon] : customExtensionIcons_) {

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
		if (texture->valid) {
			return static_cast<ImTextureID>(texture->gpuHandle.ptr);
		}
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

ImTextureID Engine::ProjectAssetThumbnailCache::GetCustomExtensionIcon(const std::string& assetPath) const {

	const std::string lowerAssetPath = Algorithm::ToLower(assetPath);
	for (const auto& [ext, icon] : customExtensionIcons_) {
		if (lowerAssetPath.length() >= ext.length() &&
			lowerAssetPath.compare(lowerAssetPath.length() - ext.length(), ext.length(), ext) == 0) {
			ImTextureID id = TryGetTextureID(icon.textureKey);
			if (id != ImTextureID{}) {
				return id;
			}
		}
	}
	return ImTextureID{};
}

ImTextureID Engine::ProjectAssetThumbnailCache::GetAssetTextureID(const std::string& assetPath, AssetType type) {

	if (!initialized_ || !textureUploadService_) {
		return ImTextureID{};
	}

	if (type != AssetType::Texture) {
		ImTextureID customIcon = GetCustomExtensionIcon(assetPath);
		if (customIcon != ImTextureID{}) {
			return customIcon;
		}
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
