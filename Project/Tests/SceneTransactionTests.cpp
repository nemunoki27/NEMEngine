#include "TestContracts.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabDocument.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabBaseDocument.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

namespace NEMTests {

	bool TestSceneSnapshotTransactions() {

		using namespace Engine;
		if (!TestCreationScopes()) {
			return false;
		}
		std::string nameBase = "Unchanged";
		uint32_t nameIndex = 23;
		for (const auto& name : { "Entity_4294967296", "Entity_+1", "Entity_12x", "Entity_-1" }) {
			if (SceneAuthoring::TryParseIndexedName(name, nameBase, nameIndex) || nameBase != "Unchanged" || nameIndex != 23) {
				return false;
			}
		}
		if (SceneAuthoring::TryParseIndexedName("Entity_" + std::string(1000, '9'), nameBase, nameIndex) ||
			!SceneAuthoring::TryParseIndexedName("Entity_4294967295", nameBase, nameIndex) || nameBase != "Entity" || nameIndex != UINT32_MAX) {
			return false;
		}
		TestDirectory directory("SceneTransactions", RuntimePaths::GetGameAssetsRoot());
		AssetDatabase database;
		database.Init();
		const auto scenePath = directory.GetPath() / "Test.scene.json";
		if (!JsonFile::Save(scenePath, nlohmann::json::object())) {
			return false;
		}
		const auto asset = database.ImportOrGet(RuntimePaths::ToAssetPath(scenePath), AssetType::Scene);
		SceneHeader header;
		header.guid = asset;
		header.name = "Original";
		const auto previousHeader = ToJson(header);
		for (const auto& invalid : { nlohmann::json{}, nlohmann::json::object(), nlohmann::json(123) }) {
			auto malformed = previousHeader;
			malformed["name"] = "Changed";
			malformed["subScenes"] = invalid;
			if (FromJson(malformed, header, &database) || ToJson(header) != previousHeader || header.guid != asset) return false;
		}
		auto slots = previousHeader;
		const nlohmann::json slot = { { "slotID", ToString(Engine::UUID{ 77 }) }, { "slotName", "Unassigned" },
			{ "sceneAsset", "" }, { "enabled", false } };
		slots["subScenes"] = nlohmann::json::array({ slot, slot });
		if (FromJson(slots, header, &database) || ToJson(header) != previousHeader) return false;
		slots["subScenes"] = nlohmann::json::array({ slot });
		slots["subScenes"][0]["enabled"] = "invalid";
		if (FromJson(slots, header, &database) || ToJson(header) != previousHeader) return false;
		slots["subScenes"][0] = slot;
		// 未割当のSubSceneスロットも読込と保存で維持する
		if (!FromJson(slots, header, &database) || ToJson(header) != slots || header.guid != asset) return false;
		header.subScenes.clear();
		// Headerを受け取らない呼出しでも破損入力を拒否する
		nlohmann::json sceneFile = { { "SchemaVersion", 3 }, { "Header", ToJson(header) },
			{ "Entities", nlohmann::json::array() }, { "PrefabInstances", nlohmann::json::array() } };
		sceneFile["Header"]["subScenes"] = nlohmann::json::object();
		SceneSystem validationSystem;
		ECSWorld validationWorld;
		if (!JsonFile::Save(scenePath, sceneFile) ||
			validationSystem.LoadScene(scenePath, validationWorld, &database, asset)) return false;
		sceneFile["Header"] = ToJson(header);
		// 失敗した読込のディスク状態を次回保存の基準にしない
		SceneSaveSnapshot afterFailedLoad;
		afterFailedLoad.scenePath = scenePath;
		afterFailedLoad.sceneAsset = asset;
		afterFailedLoad.root = sceneFile;
		std::string saveError;
		if (!JsonFile::Save(scenePath, sceneFile) ||
			!validationSystem.GetStorage()->Save(afterFailedLoad, saveError)) return false;
		sceneFile["Entities"] = nlohmann::json::object();
		sceneFile["ExternalActors"] = nlohmann::json::array();
		if (SceneDocument::ValidateSceneFileRoot(sceneFile)) return false;
		sceneFile.erase("Entities");
		if (!SceneDocument::ValidateSceneFileRoot(sceneFile)) return false;
		// 小数や桁あふれを有効な版番号へ読み替えない
		for (const auto& invalid : nlohmann::json::array({ nullptr, "3", true, 3.0, 3.5, -1, 4294967299ULL })) {
			sceneFile["SchemaVersion"] = invalid;
			if (SceneDocument::ValidateSceneFileRoot(sceneFile)) return false;
		}
		sceneFile.erase("SchemaVersion");
		if (SceneDocument::ValidateSceneFileRoot(sceneFile)) return false;
		const nlohmann::json entity = {
			{ "LocalFileID", "0000000000000001" }, { "Components", nlohmann::json::object() }
		};
		const nlohmann::json scene = {
			{ "InstanceID", "0000000000000001" }, { "SceneAsset", ToString(asset) },
			{ "Header", ToJson(header) }, { "Entities", nlohmann::json::array({ entity }) }
		};
		nlohmann::json snapshot = {
			{ "ActiveInstance", "0000000000000002" }, { "Scenes", nlohmann::json::array({ scene, scene }) }
		};
		snapshot["Scenes"][1]["InstanceID"] = "0000000000000002";
		snapshot["Scenes"][1]["Entities"][0]["Components"]["MissingComponent"] = nlohmann::json::object();
		ECSWorld world;
		SceneSystem system;
		SceneInstanceManager scenes;
		// 二つ目のSceneで失敗したら一つ目の実体も残さない
		bool passed = asset && !scenes.LoadSnapshot(database, system, world, snapshot) &&
			scenes.GetAll().empty() && scenes.GetRevision() == 0;
		size_t alive = 0;
		world.ForEachAliveEntity([&](Entity) { ++alive; });
		passed &= alive == 0;
		snapshot["Scenes"][1]["Entities"][0]["Components"] = nlohmann::json::object();
		passed &= scenes.LoadSnapshot(database, system, world, snapshot) && scenes.GetAll().size() == 2;
		SceneSaveSnapshot captured;
		// Assetだけでは曖昧な保存を拒否し、指定したInstanceの内容を採る
		passed &= !scenes.CaptureSave(database, system, world, asset, captured);
		const auto* target = scenes.Find(Engine::UUID{ 2 });
		if (!target || target->createdEntities.size() != 1) {
			return false;
		}
		const Entity saved = target->createdEntities.front();
		passed &= !SceneObjectUtility::ResolveReference(world, asset, Engine::UUID{ 1 }).IsValid() &&
			SceneObjectUtility::ResolveReference(world, asset, Engine::UUID{ 1 }, Engine::UUID{ 2 }) == saved &&
			!SceneObjectUtility::ResolveReference(world, AssetID{ 99, 100 }, Engine::UUID{ 1 }).IsValid();
		const auto revision = scenes.GetRevision();
		passed &= scenes.CaptureSave(database, system, world, asset, captured, Engine::UUID{ 2 }) &&
			captured.root["Entities"].size() == 1;
		// 読込済みWorldへのSnapshot適用は既存実体を壊さない
		passed &= !scenes.LoadSnapshot(database, system, world, snapshot) && world.IsAlive(saved) &&
			scenes.GetAll().size() == 2 && scenes.GetRevision() == revision;

		// 生存中のUUIDを再登録しても既存の検索結果を壊さない
		const auto stableID = world.GetUUID(saved);
		bool rejected = false;
		try {
			world.CreateEntity(stableID);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		passed &= rejected && world.FindByUUID(stableID) == saved;

		// 同じPrefabを含むSceneを二重に読み込み、保存元IDは変えない
		const auto prefabPath = directory.GetPath() / "Test.prefab.json";
		const nlohmann::json prefab = {
			{ "SchemaVersion", 2 }, { "Header", { { "rootLocalFileID", "0000000000000001" } } },
			{ "Entities", nlohmann::json::array({ entity }) }
		};
		if (!JsonFile::Save(prefabPath, prefab)) {
			return false;
		}
		PrefabInstanceData prefabData;
		prefabData.prefabAsset = database.ImportOrGet(RuntimePaths::ToAssetPath(prefabPath), AssetType::Prefab);
		// 生成用と差分比較用の読込で同じ版番号を検証する
		for (const auto& invalid : nlohmann::json::array({ nullptr, "2", true, 2.0, 2.5, -1, 4294967298ULL })) {
			auto malformed = prefab;
			malformed["SchemaVersion"] = invalid;
			std::filesystem::path loadedPath;
			nlohmann::json loaded;
			if (!JsonFile::Save(prefabPath, malformed) ||
				PrefabDocument::Read(database, prefabData.prefabAsset, loadedPath, loaded) ||
				!PrefabBaseDocument::LoadPrefabBaseEntities(database, prefabData.prefabAsset).empty()) return false;
		}
		if (!JsonFile::Save(prefabPath, prefab) ||
			PrefabBaseDocument::LoadPrefabBaseEntities(database, prefabData.prefabAsset).size() != 1) return false;
		prefabData.instanceID = Engine::UUID{ 30 };
		prefabData.entityMap = { { Engine::UUID{ 1 }, Engine::UUID{ 10 } } };
		const nlohmann::json prefabScene = {
			{ "SchemaVersion", SceneDocument::kSceneSchemaVersion }, { "Header", ToJson(header) }, { "Entities", nlohmann::json::array() },
			{ "PrefabInstances", nlohmann::json::array({ ToJson(prefabData) }) }
		};
		if (!JsonFile::Save(scenePath, prefabScene)) {
			return false;
		}
		ECSWorld prefabWorld;
		SceneInstanceManager prefabScenes;
		if (!prefabScenes.LoadAdditive(database, system, prefabWorld, asset, Engine::UUID{ 10 }) ||
			!prefabScenes.LoadAdditive(database, system, prefabWorld, asset, Engine::UUID{ 20 })) {
			return false;
		}
		const auto* first = prefabScenes.Find(Engine::UUID{ 10 });
		const auto* second = prefabScenes.Find(Engine::UUID{ 20 });
		if (!first || !second || first->createdEntities.size() != 1 || second->createdEntities.size() != 1) {
			return false;
		}
		const auto firstID = prefabWorld.GetComponent<PrefabLinkComponent>(first->createdEntities.front()).prefabInstanceID;
		const auto secondID = prefabWorld.GetComponent<PrefabLinkComponent>(second->createdEntities.front()).prefabInstanceID;
		SceneSaveSnapshot firstSave;
		SceneSaveSnapshot secondSave;
		passed &= firstID != secondID &&
			prefabScenes.CaptureSave(database, system, prefabWorld, asset, firstSave, Engine::UUID{ 10 }) &&
			prefabScenes.CaptureSave(database, system, prefabWorld, asset, secondSave, Engine::UUID{ 20 });
		passed &= firstSave.root == secondSave.root && firstSave.root["PrefabInstances"][0]["InstanceID"] == ToString(prefabData.instanceID);

		std::vector<SceneSaveSnapshot> allSaves;
		std::vector<AssetID> conflicts;
		passed &= prefabScenes.CaptureAllSaves(database, system, prefabWorld, allSaves, conflicts) && allSaves.size() == 1;
		prefabWorld.GetComponent<NameComponent>(second->createdEntities.front()).name = "Different";
		// 競合時は既存の保存候補を保持し、選択後は指定Instanceだけを採る
		passed &= !prefabScenes.CaptureAllSaves(database, system, prefabWorld, allSaves, conflicts) &&
			conflicts == std::vector<AssetID>{ asset } && allSaves.size() == 1 && allSaves.front().root == firstSave.root;
		passed &= prefabScenes.CaptureAllSaves(database, system, prefabWorld, allSaves, conflicts,
			{ { asset, Engine::UUID{ 20 } } }) && allSaves.size() == 1 && allSaves.front().root != firstSave.root;
		passed &= !prefabScenes.CaptureAllSaves(database, system, prefabWorld, allSaves, conflicts,
			{ { asset, Engine::UUID{ 99 } } });
		ECSWorld restoredWorld;
		SceneInstanceManager restoredScenes;
		passed &= restoredScenes.LoadSnapshot(database, system, restoredWorld, prefabScenes.SerializeSnapshot(system, prefabWorld));
		passed &= SceneObjectUtility::ResolveReference(restoredWorld, prefabData.prefabAsset, Engine::UUID{ 10 }, Engine::UUID{ 20 }).IsValid();
		return passed;
	}
}
