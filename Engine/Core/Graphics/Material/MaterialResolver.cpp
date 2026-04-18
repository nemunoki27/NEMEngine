#include "MaterialResolver.h"

//============================================================================
//	MaterialResolver classMethods
//============================================================================

Engine::AssetID Engine::MaterialResolver::ResolveORDefault(AssetDatabase& database, AssetID requested, DefaultMaterialSlot slot)  const {

	// 要求されたIDが有効であればそれを返す
	if (requested) {
		return requested;
	}

	// デフォルトマテリアルのIDを確保する
	EnsureDefaults(database);
	// スロットに応じたデフォルトマテリアルのIDを返す
	const AssetID defaultMaterial = defaultMaterials_[static_cast<size_t>(slot)];
	return defaultMaterial;
}

void Engine::MaterialResolver::Clear() {

	initialized_ = false;
	defaultMaterials_.fill(AssetID());
}

void Engine::MaterialResolver::EnsureDefaults(AssetDatabase& database) const {

	if (initialized_) {
		return;
	}

	// ID配列を無効なIDで初期化
	defaultMaterials_.fill(AssetID{});

	auto tryImportIfExists = [&](DefaultMaterialSlot slot, AssetType type) {
		std::filesystem::path fullPath = database.GetProjectRoot() / GetDefaultAssetPath(slot);
		if (!std::filesystem::exists(fullPath)) {
			return;
		}
		// デフォルトマテリアルのパスが存在する場合はインポートしてIDを確保する
		defaultMaterials_[static_cast<size_t>(slot)] = database.ImportOrGet(GetDefaultAssetPath(slot), type);
		};

	// デフォルトマテリアルのパスが存在する場合はインポートしてIDを確保する
	tryImportIfExists(DefaultMaterialSlot::Sprite, AssetType::Material);
	tryImportIfExists(DefaultMaterialSlot::Text, AssetType::Material);
	tryImportIfExists(DefaultMaterialSlot::Mesh, AssetType::Material);
	tryImportIfExists(DefaultMaterialSlot::FullscreenCopy, AssetType::Material);

	// 初期化済み
	initialized_ = true;
}

const char* Engine::MaterialResolver::GetDefaultAssetPath(DefaultMaterialSlot slot) {

	// スロットに応じたデフォルトマテリアルのパスを返す
	switch (slot) {
	case DefaultMaterialSlot::Sprite:
		return "Assets/Materials/Builtin/Sprite/defaultSprite.material.json";
	case DefaultMaterialSlot::Text:
		return "Assets/Materials/Builtin/Text/defaultText.material.json";
	case DefaultMaterialSlot::Mesh:
		return "Assets/Materials/Builtin/Mesh/defaultMesh.material.json";
	case DefaultMaterialSlot::FullscreenCopy:
		return "Assets/Materials/Builtin/FullscreenCopy/fullscreenCopy.material.json";
	}
	return "";
}