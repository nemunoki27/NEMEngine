#include "SceneStorageTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/StorageFileUtility.h>

// c++
#include <iostream>
#include <array>

bool TestSceneStorageSession() {

	NEMTests::TestDirectory directory("StorageSession", Engine::RuntimePaths::GetGameAssetsRoot());
	const auto& root = directory.GetPath();
	const auto path = root / "Session.scene.json";
	std::filesystem::create_directories(root);
	nlohmann::json document = {{"SchemaVersion", 3}, {"Header", Engine::ToJson(Engine::SceneHeader{})},
		{"Entities", nlohmann::json::array()}, {"PrefabInstances", nlohmann::json::array()}};
	bool passed = Engine::JsonAdapter::SaveCanonical(path, document);
	Engine::AssetDatabase database;
	database.Init();
	const Engine::AssetID asset = database.ImportOrGet(Engine::RuntimePaths::ToAssetPath(path), Engine::AssetType::Scene);
	Engine::SceneSaveSnapshot snapshot;
	std::weak_ptr<Engine::SceneAssetStorage> retained;
	{
		Engine::SceneSystem sceneSystem;
		Engine::ECSWorld world;
		Engine::SceneHeader header;
		passed &= sceneSystem.LoadScene(path, world, &database, asset, Engine::UUID::New(), &header);
		passed &= sceneSystem.CaptureSaveSnapshot(path, world, header, database, snapshot);
		retained = sceneSystem.GetStorage();
	}
	passed &= !retained.expired();

	// 保存要求後の外部変更は所有元の終了後も検出する
	document["Header"]["name"] = "ExternalEdit";
	passed &= Engine::JsonAdapter::SaveCanonical(path, document);
	passed &= !Engine::SceneSystem::WriteSaveSnapshot(snapshot);
	passed &= Engine::JsonAdapter::Load(path) == document;

	// 別セッションでは再読込した状態を基準に保存できる
	{
		Engine::SceneSystem sceneSystem;
		Engine::ECSWorld world;
		Engine::SceneHeader header;
		Engine::SceneSaveSnapshot current;
		passed &= sceneSystem.LoadScene(path, world, &database, asset, Engine::UUID::New(), &header);
		passed &= sceneSystem.CaptureSaveSnapshot(path, world, header, database, current);
		passed &= Engine::SceneSystem::WriteSaveSnapshot(std::move(current));
	}
	snapshot = {};
	passed &= retained.expired();

	// 付随ファイルの削除失敗では本体とmetaも元のbyteへ戻す
	const auto binary = root / "a_asset.bin";
	const auto metadata = root / "a_asset.bin.meta";
	const auto sidecar = root / "z_sidecar.bin";
	passed &= Engine::StorageFileUtility::WriteBytes(binary, std::string("a\0bc", 4));
	passed &= Engine::JsonAdapter::Save(metadata, {{"sentinel", 17}});
	passed &= Engine::StorageFileUtility::WriteBytes(sidecar, std::string("d\0ef", 4));
	const std::array paths{binary, metadata, sidecar};
	const std::array revisions{Engine::StorageFileUtility::FileRevision(binary),
		Engine::StorageFileUtility::FileRevision(metadata), Engine::StorageFileUtility::FileRevision(sidecar)};
	const std::array additional{metadata, sidecar};
	Engine::SceneAssetStorage storage;
	std::string error;
	{
		NEMTests::TestFileReadLock lock(sidecar);
		passed &= !storage.Delete(binary, database, error, additional);
		for (size_t index = 0; index < paths.size(); ++index) {
			passed &= Engine::StorageFileUtility::FileRevision(paths[index]) == revisions[index];
		}
	}
	// 削除途中まで進み、復旧されたことも確認する
	bool recovered = false;
	for (const auto& recovery : directory.GetStorageRecoveries()) {
		const auto record = Engine::JsonAdapter::Load(recovery / "operation.json", false);
		recovered |= record.value("label", "") == "アセット削除" && record.value("state", "") == "recovered" &&
					 record.at("files").size() == paths.size();
	}
	passed &= recovered;
	// 範囲外の付随ファイルは削除開始前に拒否する
	const std::array outside{Engine::RuntimePaths::GetGameAssetsRoot()};
	passed &= !storage.Delete(binary, database, error, outside);
	for (size_t index = 0; index < paths.size(); ++index) {
		passed &= Engine::StorageFileUtility::FileRevision(paths[index]) == revisions[index];
	}
	// 無関係な不正Sceneがあっても通常Assetだけを削除できる
	const auto savedScene = Engine::JsonAdapter::Load(path, false);
	passed &= Engine::StorageFileUtility::WriteBytes(path, "{");
	passed &= !database.RefreshDependencies(asset);
	const auto sceneRevision = Engine::StorageFileUtility::FileRevision(path);
	passed &= storage.Delete(binary, database, error, additional);
	for (const auto& file : paths) {
		passed &= !std::filesystem::exists(file);
	}
	passed &= Engine::StorageFileUtility::FileRevision(path) == sceneRevision;
	passed &= Engine::JsonAdapter::SaveCanonical(path, savedScene);
	passed &= directory.Remove();
	if (!passed) {
		std::cerr << "Scene storage session or asset deletion failed\n";
	}
	return passed;
}
