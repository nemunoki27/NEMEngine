#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Rendering/Materials/MaterialResolver.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Assets/BuiltinAssetIDs.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>

bool NEMTests::TestMaterialResolverIndexChanges() {

	using namespace Engine;
	TestDirectory directory("MaterialResolver", RuntimePaths::GetGameAssetsRoot() / "Materials");
	auto path = directory.GetPath() / "Builtin.material.json";
	AssetDatabase database;
	database.Init();
	MaterialResolver resolver;
	AssetID requested{1, 2};
	// 指定IDは未解決でも維持し、空IDだけ既定へ戻す
	if (resolver.ResolveORDefault(database, requested, DefaultMaterialSlot::Mesh) != requested ||
		resolver.ResolveORDefault(database, {}, DefaultMaterialSlot::Mesh)) {
		return false;
	}
	AssetMeta meta;
	meta.guid = BuiltinAssets::Materials::DefaultMesh;
	meta.type = AssetType::Material;
	meta.assetPath = RuntimePaths::ToAssetPath(path);
	auto metaPath = path;
	metaPath += ".meta";
	if (!JsonFile::Save(path, nlohmann::json{{"name", "BuiltinFixture"}}) ||
		!AssetDatabase::WriteMetaFile(metaPath, meta) ||
		database.ImportOrGet(meta.assetPath, AssetType::Material) != meta.guid) {
		return false;
	}
	// 初回解決後の登録も次の解決へ反映する
	if (resolver.ResolveORDefault(database, {}, DefaultMaterialSlot::Mesh) != meta.guid) {
		return false;
	}
	AssetDatabase empty;
	empty.Init();
	if (resolver.ResolveORDefault(empty, {}, DefaultMaterialSlot::Mesh)) {
		return false;
	}
	// 削除後の索引で古いGUIDを返さない
	std::filesystem::remove(path);
	if (!database.RebuildMeta({directory.GetPath()}) ||
		resolver.ResolveORDefault(database, {}, DefaultMaterialSlot::Mesh)) {
		return false;
	}
	// 同じGUIDでもMaterial以外は採用しない
	meta.type = AssetType::Texture;
	if (!JsonFile::Save(path, nlohmann::json{{"name", "WrongType"}}) ||
		!AssetDatabase::WriteMetaFile(metaPath, meta) ||
		empty.ImportOrGet(meta.assetPath, AssetType::Texture) != meta.guid) {
		return false;
	}
	return !resolver.ResolveORDefault(empty, {}, DefaultMaterialSlot::Mesh) &&
		!resolver.ResolveORDefault(empty, {}, static_cast<DefaultMaterialSlot>(255));
}
