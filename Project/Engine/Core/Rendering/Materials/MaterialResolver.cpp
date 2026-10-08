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
Engine::AssetID Engine::MaterialResolver::ResolveORDefault(
	const AssetDatabase& database, AssetID requested, DefaultMaterialSlot slot) const {

	// 指定されたIDを優先する
	if (requested) {
		return requested;
	}

	// 登録済みの設定MaterialをBuiltinより優先する
	AssetID configured{};
	switch (slot) {
	case DefaultMaterialSlot::Mesh:   configured = DefaultMaterialSettings::GetInstance().GetMesh();   break;
	case DefaultMaterialSlot::Sprite: configured = DefaultMaterialSettings::GetInstance().GetSprite(); break;
	case DefaultMaterialSlot::Text:   configured = DefaultMaterialSettings::GetInstance().GetText();   break;
	case DefaultMaterialSlot::Line:   configured = DefaultMaterialSettings::GetInstance().GetLine();   break;
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

	// 固定GUIDを現在の索引で確認する
	AssetID assetID = GetDefaultAssetID(slot);
	const AssetMeta* meta = database.Find(assetID);
	return meta && meta->type == AssetType::Material ? assetID : AssetID{};
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
	case DefaultMaterialSlot::Primitive:
		return BuiltinAssets::Materials::DefaultPrimitive;
	case DefaultMaterialSlot::Primitive2D:
		return BuiltinAssets::Materials::DefaultPrimitive2D;
	}
	return AssetID{};
}
