#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/ContentHash.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestSceneAssetStorage() {

		using Storage = Engine::SceneAssetStorage;
		Storage storage;
		TestDirectory directory("Storage", Engine::RuntimePaths::GetGameAssetsRoot());
		const auto& testRoot = directory.GetPath();
		const auto scenePath = testRoot / "Source.scene.json";
		const auto referencerPath = testRoot / "Referencer.scene.json";
		std::filesystem::create_directories(testRoot);
		bool passed = true;
		std::string error;
		auto check = [&](bool condition, const char* name) {
			if (!condition) std::cerr << "Scene storage: " << name << " / " << error << '\n';
			passed &= condition;
		};
		const nlohmann::json emptyScene = {{ "SchemaVersion", 3 }, { "Header", Engine::ToJson(Engine::SceneHeader{}) },
			{ "Entities", nlohmann::json::array() }, { "PrefabInstances", nlohmann::json::array() }};
		Engine::JsonAdapter::SaveCanonical(scenePath, emptyScene);
		Engine::AssetDatabase database;
		database.Init();
		const Engine::AssetID id = database.ImportOrGet(Engine::RuntimePaths::ToAssetPath(scenePath), Engine::AssetType::Scene);
		const Engine::UUID parentID = Engine::UUID::New();
		const Engine::UUID childID = Engine::UUID::New();
		const auto actorRoot = Storage::ResolveActorRoot(scenePath, id);
		const auto parentPath = actorRoot / (Engine::ToString(parentID) + ".actor.json");
		const auto childPath = actorRoot / (Engine::ToString(childID) + ".actor.json");
		auto actor = [](Engine::UUID actorID, Engine::UUID parent) {
			return nlohmann::json{{ "LocalFileID", Engine::ToString(actorID) }, { "Components", {
				{ "Hierarchy", {{ "parentLocalFileID", parent ? Engine::ToString(parent) : "" }} },
				{ "Name", {{ "name", "StorageActor" }} } } }};
		};
		Engine::SceneSaveSnapshot snapshot;
		snapshot.scenePath = scenePath;
		snapshot.sceneAsset = id;
		snapshot.useExternalActors = true;
		snapshot.root = emptyScene;
		snapshot.root["Entities"] = { actor(parentID, {}), actor(childID, parentID) };
		check(storage.Save(snapshot, error), "initial save");
		if (!passed) return false;
		check(storage.Validate(scenePath, id).empty(), "validate saved actors");
		const auto validScene = Engine::JsonAdapter::Load(scenePath, false);
		auto duplicateScene = validScene;
		duplicateScene["ExternalActors"].push_back(Engine::ToString(parentID));
		Engine::JsonAdapter::SaveCanonical(scenePath, duplicateScene);
		check(!storage.Validate(scenePath, id).empty(), "duplicate actor rejected");
		duplicateScene["ExternalActors"][0] = "../outside";
		Engine::JsonAdapter::SaveCanonical(scenePath, duplicateScene);
		check(!storage.Validate(scenePath, id).empty(), "invalid actor path rejected");
		Engine::JsonAdapter::SaveCanonical(scenePath, validScene);
		const auto undoSnapshot = snapshot;
		snapshot.root["Entities"] = nlohmann::json::array({ actor(childID, {}) });
		check(storage.Save(snapshot, error) && !std::filesystem::exists(parentPath), "delete and save");
		check(storage.Save(undoSnapshot, error) && std::filesystem::exists(parentPath), "undo and save");
		check(storage.Save(snapshot, error) && !std::filesystem::exists(parentPath), "redo and save");
		const auto beforeFailure = Engine::ContentHash::FileSHA256(childPath);
		auto failing = snapshot;
		failing.root["Entities"][0]["Components"]["Name"]["name"] = "Changed";
		failing.root["Header"]["name"] = "Changed";
		{
			TestFileReadLock lockedScene(scenePath);
			storage.SetProtectedScenes({ id });
			check(!storage.Save(failing, error), "save failure is reported");
			check(Engine::ContentHash::FileSHA256(childPath) == beforeFailure, "rollback restores changed actor");
			storage.SetProtectedScenes({});
		}
		const auto actorBackup = testRoot / "Original.actor.json";
		std::filesystem::copy_file(childPath, actorBackup);
		std::filesystem::remove(childPath);
		check(storage.Validate(scenePath, id).size() == 1, "missing actor detected");
		check(!storage.Save(snapshot, error), "external deletion blocks save");
		check(storage.RestoreActor(scenePath, childID, actorBackup, error), "restore original actor");
		check(!storage.RestoreActor(scenePath, childID, actorBackup, error), "do not overwrite actor");
		check(storage.Save(undoSnapshot, error), "restore parent and child");
		std::filesystem::remove(parentPath);
		auto child = Engine::JsonAdapter::Load(childPath, false);
		child["Components"]["UnknownReference"] = Engine::ToString(parentID);
		Engine::JsonAdapter::SaveCanonical(childPath, child);
		check(!storage.RemoveMissingActor(scenePath, parentID, error), "unknown reference blocks removal");
		child["Components"].erase("UnknownReference");
		const auto otherScene = Engine::AssetGUID::New();
		const nlohmann::json targetReference = {{ "kind", "Scene" }, { "sourceAsset", Engine::ToString(id) }, { "localFileId", Engine::ToString(parentID) }};
		child["Components"]["TargetReference"] = targetReference;
		child["Components"]["OtherReference"] = {{ "kind", "Scene" }, { "sourceAsset", Engine::ToString(otherScene) }, { "localFileId", Engine::ToString(parentID) }};
		Engine::JsonAdapter::SaveCanonical(childPath, child);
		const auto prefabPath = testRoot / "References.prefab.json";
		Engine::JsonAdapter::SaveCanonical(prefabPath, {{ "reference", targetReference }});
		std::vector<std::filesystem::path> affectedFiles;
		check(storage.PreviewMissingActorRemoval(scenePath, parentID, affectedFiles, error), "preview missing actor removal");
		check(affectedFiles.size() == 3, "preview lists scene child and external reference");
		check(Engine::JsonAdapter::Load(childPath, false) == child, "preview does not change files");
		check(storage.RemoveMissingActor(scenePath, parentID, error), "confirm missing parent deletion");
		check(Engine::JsonAdapter::Load(childPath)["Components"]["Hierarchy"]["parentLocalFileID"] == "", "preserve child at root");
		const auto repairedChild = Engine::JsonAdapter::Load(childPath, false);
		check(repairedChild["Components"]["TargetReference"]["kind"] == "Null", "clear typed scene reference");
		check(repairedChild["Components"]["OtherReference"]["kind"] == "Scene", "preserve other scene reference");
		check(Engine::JsonAdapter::Load(prefabPath, false)["reference"]["kind"] == "Null", "clear cross asset reference");
		check(storage.Validate(scenePath, id).empty(), "validate repaired scene");
		const auto repairedScene = Engine::JsonAdapter::Load(scenePath, false);
		storage.TrackLoaded(scenePath, id);
		std::filesystem::remove(scenePath);
		check(!storage.Save(snapshot, error), "external scene deletion blocks save");
		Engine::JsonAdapter::SaveCanonical(scenePath, repairedScene);
		storage.SetProtectedScenes({ id });
		check(!storage.Delete(scenePath, database, error), "loaded scene protected");
		storage.SetProtectedScenes({});
		auto referencedScene = emptyScene;
		referencedScene["Header"]["subScenes"] = {{{ "scene", Engine::ToString(id) }}};
		Engine::JsonAdapter::SaveCanonical(referencerPath, referencedScene);
		const auto referencerID = database.ImportOrGet(Engine::RuntimePaths::ToAssetPath(referencerPath), Engine::AssetType::Scene);
		check(!storage.Delete(scenePath, database, error), "referenced scene protected");
		Engine::SceneSaveSnapshot referenceSnapshot;
		referenceSnapshot.scenePath = referencerPath;
		referenceSnapshot.sceneAsset = referencerID;
		referenceSnapshot.useExternalActors = true;
		referenceSnapshot.root = emptyScene;
		auto referenceActor = actor(Engine::UUID::New(), {});
		referenceActor["Components"]["Script"] = nlohmann::json::array({ {{ "serializedFields", {{ "type", "AssetRef" }, { "value", {{ "assetId", Engine::ToString(id) }} }} }} });
		referenceSnapshot.root["Entities"] = nlohmann::json::array({ referenceActor });
		check(storage.Save(referenceSnapshot, error), "save external actor reference");
		check(!storage.Delete(scenePath, database, error), "external actor scene reference protected");
		const auto referencerActorRoot = Storage::ResolveActorRoot(referencerPath, referencerID);
		check(!storage.Delete(Engine::RuntimePaths::GetGameAssetsRoot(), database, error), "asset root protected");
		directory.CaptureSceneAssets();
		check(storage.Delete(testRoot, database, error), "delete containing directory and owned actors");
		check(!std::filesystem::exists(scenePath) && !std::filesystem::exists(actorRoot) && !std::filesystem::exists(referencerActorRoot), "no remaining owned actors");
		const auto records = directory.GetSceneRecoveries();
		bool recoveredDeletion = false;
		for (const auto& recovery : records) {
			const auto record = Engine::JsonAdapter::Load(recovery / "operation.json", false);
			if (record.value("label", "") == "アセット削除" && record.value("state", "") == "completed") {
				storage.SetProtectedScenes({ id });
				check(!storage.Recover(recovery, error), "deleted loaded scene protected during recovery");
				storage.SetProtectedScenes({});
				auto interrupted = record;
				interrupted["state"] = "pending";
				Engine::JsonAdapter::SaveCanonical(recovery / "operation.json", interrupted);
				std::filesystem::rename(recovery / "operation.json", recovery / "operation.json.bak");
				check(!storage.GetRecoveries(true).empty(), "interrupted operation detected");
				check(!storage.Delete(testRoot, database, error), "pending recovery blocks new operations");
				check(storage.Recover(recovery, error), "recover deleted scene recovery");
				recoveredDeletion = true;
				check(std::filesystem::exists(scenePath) && std::filesystem::exists(childPath), "recovery includes external actors");
			}
		}
		check(recoveredDeletion, "deletion recovery record exists");
		return directory.Remove() && passed && TestSceneStorageSession();
	}

	bool TestExternalActors() {

		TestDirectory directory("ExternalActors", Engine::RuntimePaths::GetGameAssetsRoot());
		const auto& testRoot = directory.GetPath();
		std::error_code ec;
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		Engine::AssetDatabase database;
		database.Init();
		const std::filesystem::path scenePath =
			testRoot / "ExternalActors.scene.json";
		{
			const nlohmann::json emptyScene = {
				{ "SchemaVersion", 3 },
				{ "Header", Engine::ToJson(Engine::SceneHeader{}) },
				{ "ExternalActors", nlohmann::json::array() },
				{ "PrefabInstances", nlohmann::json::array() },
			};
			if (!Engine::JsonAdapter::SaveCanonical(scenePath, emptyScene)) {
				return false;
			}
		}
		const Engine::AssetID sceneAsset = database.ImportOrGet(
			Engine::RuntimePaths::ToAssetPath(testRoot / "ExternalActors.scene.json"), Engine::AssetType::Scene);

		Engine::ECSWorld sourceWorld;
		const Engine::Entity sourceEntity = sourceWorld.CreateEntity();
		Engine::SceneAuthoring::EnsureGameObjectDefaults(
			sourceWorld, sourceEntity, "ExternalActor");
		const Engine::UUID localFileID =
			sourceWorld.GetComponent<Engine::SceneObjectComponent>(
				sourceEntity).localFileID;

		Engine::SceneHeader header{};
		header.guid = sceneAsset;
		header.name = "ExternalActors";
		Engine::SceneSystem sceneSystem;
		Engine::SceneSaveSnapshot externalSnapshot{};
		bool passed = sceneSystem.CaptureSaveSnapshot(
			scenePath, sourceWorld, header, database,
			externalSnapshot);
		if (passed) {
			externalSnapshot.useExternalActors = true;
			passed = Engine::SceneSystem::WriteSaveSnapshot(
				std::move(externalSnapshot));
		}

		const nlohmann::json savedScene = Engine::JsonAdapter::Load(scenePath);
		const std::filesystem::path actorRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" /
			Engine::ToString(sceneAsset);
		const std::filesystem::path actorPath =
			actorRoot / (Engine::ToString(localFileID) + ".actor.json");
		passed &= savedScene.value("SchemaVersion", 0) == 3 &&
			!savedScene.contains("Entities") &&
			savedScene.contains("ExternalActors") &&
			savedScene["ExternalActors"].is_array() &&
			savedScene["ExternalActors"].size() == 1 &&
			std::filesystem::exists(actorPath);

		Engine::ECSWorld loadedWorld;
		std::vector<Engine::Entity> loadedEntities;
		passed &= sceneSystem.LoadScene(scenePath, loadedWorld, &database,
			sceneAsset, Engine::UUID{ 300 }, nullptr, &loadedEntities);
		passed &= loadedEntities.size() == 1 &&
			loadedWorld.IsAlive(loadedEntities.front()) &&
			loadedWorld.GetComponent<Engine::SceneObjectComponent>(
				loadedEntities.front()).localFileID == localFileID;

		database.RebuildMeta();
		const bool actorWasImported = std::any_of(
			database.GetAssets().begin(), database.GetAssets().end(),
			[](const auto& entry) {
				return entry.second.assetPath.ends_with(".actor.json");
			});
		passed &= !actorWasImported;

		Engine::SceneSaveSnapshot monolithicSnapshot{};
		passed &= sceneSystem.CaptureSaveSnapshot(
			scenePath, sourceWorld, header, database,
			monolithicSnapshot);
		if (passed) {
			monolithicSnapshot.useExternalActors = false;
			passed &= Engine::SceneSystem::WriteSaveSnapshot(
				std::move(monolithicSnapshot));
		}
		const nlohmann::json monolithicScene =
			Engine::JsonAdapter::Load(scenePath);
		passed &= monolithicScene.value("SchemaVersion", 0) == 3 &&
			monolithicScene.contains("Entities") &&
			monolithicScene["Entities"].is_array() &&
			monolithicScene["Entities"].size() == 1 &&
			!monolithicScene.contains("ExternalActors") &&
			!std::filesystem::exists(actorRoot);

		directory.Remove();
		ec.clear();
		std::filesystem::remove_all(actorRoot, ec);
		return passed && !ec;
	}
}
