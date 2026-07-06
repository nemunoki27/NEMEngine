#include "MaterialResolver.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Rendering/Materials/DefaultMaterialSettings.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>

//============================================================================
//	MaterialResolver classMethods
//============================================================================
Engine::AssetID Engine::MaterialResolver::ResolveORDefault(AssetDatabase& database, AssetID requested, DefaultMaterialSlot slot)  const {

	// 要求されたIDが有効であればそれを返す
	if (requested) {
		return requested;
	}

	// マテリアルツールで設定されたデフォルトがあればbuiltinより優先する
	// 設定はGUID参照なのでDBに存在しMaterialのときだけ採用し、壊れていればbuiltinへ落とす
	AssetID configured{};
	switch (slot) {
	case DefaultMaterialSlot::Mesh:   configured = DefaultMaterialSettings::GetInstance().GetMesh();   break;
	case DefaultMaterialSlot::Sprite: configured = DefaultMaterialSettings::GetInstance().GetSprite(); break;
	case DefaultMaterialSlot::Text:   configured = DefaultMaterialSettings::GetInstance().GetText();   break;
	case DefaultMaterialSlot::Line:   configured = DefaultMaterialSettings::GetInstance().GetLine();   break;
	case DefaultMaterialSlot::FillMesh: configured = DefaultMaterialSettings::GetInstance().GetFillMesh(); break;
	case DefaultMaterialSlot::Primitive: configured = DefaultMaterialSettings::GetInstance().GetPrimitive(); break;
	case DefaultMaterialSlot::Primitive2D: configured = DefaultMaterialSettings::GetInstance().GetPrimitive2D(); break;
	default: break;
	}
	if (configured) {
		const AssetMeta* meta = database.Find(configured);
		if (meta && meta->type == AssetType::Material) {
			return configured;
		}
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

	auto assignIfRegistered = [&](DefaultMaterialSlot slot, AssetType type) {
		const AssetID assetID = GetDefaultAssetID(slot);
		const AssetMeta* meta = database.Find(assetID);
		if (!meta || meta->type != type) {
			return;
		}
		// .metaに登録済みのGUIDだけを使い、移動後のパスには依存しない
		defaultMaterials_[static_cast<size_t>(slot)] = assetID;
		};

	assignIfRegistered(DefaultMaterialSlot::Sprite, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::Text, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::Mesh, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::MeshOutline, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::FullscreenCopy, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::Line, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::FillMesh, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::Primitive, AssetType::Material);
	assignIfRegistered(DefaultMaterialSlot::Primitive2D, AssetType::Material);

	// 初期化済み
	initialized_ = true;
}

Engine::AssetID Engine::MaterialResolver::GetDefaultAssetID(DefaultMaterialSlot slot) {

	// スロットに応じたデフォルトマテリアルのGUIDを返す
	switch (slot) {
	case DefaultMaterialSlot::Sprite:
		return BuiltinAssets::Materials::DefaultSprite;
	case DefaultMaterialSlot::Text:
		return BuiltinAssets::Materials::DefaultText;
	case DefaultMaterialSlot::Mesh:
		return BuiltinAssets::Materials::DefaultMesh;
	case DefaultMaterialSlot::MeshOutline:
		return BuiltinAssets::Materials::DefaultMeshOutline;
	case DefaultMaterialSlot::FullscreenCopy:
		return BuiltinAssets::Materials::FullscreenCopy;
	case DefaultMaterialSlot::Line:
		return BuiltinAssets::Materials::DefaultLine;
	case DefaultMaterialSlot::FillMesh:
		return BuiltinAssets::Materials::DefaultFillMesh;
	case DefaultMaterialSlot::Primitive:
		return BuiltinAssets::Materials::DefaultPrimitive;
	case DefaultMaterialSlot::Primitive2D:
		return BuiltinAssets::Materials::DefaultPrimitive2D;
	}
	return AssetID{};
}
