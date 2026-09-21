#include "SceneStorageTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <iostream>

bool TestSceneStorageSession() {

	const auto root = Engine::RuntimePaths::GetGameAssetsRoot() / "Tests" /
		("StorageSession_" + Engine::ToString(Engine::UUID::New()));
	const auto path = root / "Session.scene.json";
	std::filesystem::create_directories(root);
	nlohmann::json document = { { "SchemaVersion", 3 }, { "Header", Engine::ToJson(Engine::SceneHeader{}) },
		{ "Entities", nlohmann::json::array() }, { "PrefabInstances", nlohmann::json::array() } };
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
	std::error_code error;
	std::filesystem::remove_all(root, error);
	passed &= !error;
	if (!passed) {
		std::cerr << "Scene storage session lifetime or isolation failed\n";
	}
	return passed;
}
