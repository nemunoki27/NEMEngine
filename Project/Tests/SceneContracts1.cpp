#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/Foundation/Identity/AssetGUID.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Components/Audio/AudioSourceComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

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

	bool TestCanonicalSceneData() {

		nlohmann::json data = nlohmann::json::object();
		data["z"] = -0.0;
		data["a"] = 1;
		const std::string serialized = Engine::JsonAdapter::SerializeCanonical(data, 2);
		if (serialized.empty() || serialized.back() != '\n' ||
			serialized.find("-0.0") != std::string::npos ||
			serialized.find("\"a\"") > serialized.find("\"z\"")) {
			return false;
		}

		Engine::PrefabInstanceData prefab{};
		prefab.entityMap.emplace_back(Engine::UUID{ 3 }, Engine::UUID{ 30 });
		prefab.entityMap.emplace_back(Engine::UUID{ 1 }, Engine::UUID{ 10 });
		prefab.modifications.push_back({ Engine::UUID{ 2 }, "Transform/localScale", 1.0f });
		prefab.modifications.push_back({ Engine::UUID{ 2 }, "Transform/localPos", 0.0f });
		const nlohmann::json prefabJson = Engine::ToJson(prefab);
		return prefabJson["EntityMap"][0]["P"] == "0000000000000001" &&
			prefabJson["Modifications"][0]["Path"] == "Transform/localPos";
	}

	bool TestSubScenes() {

		const std::filesystem::path testRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() / "Tests";
		if (testRoot.parent_path().lexically_normal() !=
			Engine::RuntimePaths::GetGameAssetsRoot().lexically_normal()) {
			return false;
		}

		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		const auto SaveScene = [](const std::filesystem::path& path,
			const Engine::SceneHeader& header) {

			const nlohmann::json root = {
				{ "SchemaVersion", 3 },
				{ "Header", Engine::ToJson(header) },
				{ "ExternalActors", nlohmann::json::array() },
				{ "PrefabInstances", nlohmann::json::array() },
			};
			return Engine::JsonAdapter::SaveCanonical(path, root);
			};

		Engine::AssetDatabase database;
		database.Init();

		const std::filesystem::path childPath = testRoot / "Child.scene.json";
		Engine::SceneHeader childHeader{};
		childHeader.name = "Child";
		if (!SaveScene(childPath, childHeader)) {
			return false;
		}
		const Engine::AssetID childAsset =
			database.ImportOrGet("game://Tests/Child.scene.json", Engine::AssetType::Scene);

		const std::filesystem::path rootPath = testRoot / "Root.scene.json";
		Engine::SceneHeader rootHeader{};
		rootHeader.name = "Root";
		rootHeader.subScenes.push_back({
			.slotID = Engine::UUID{ 101 },
			.slotName = "Child",
			.sceneAsset = childAsset,
			.enabled = true,
			});
		if (!SaveScene(rootPath, rootHeader)) {
			return false;
		}
		const Engine::AssetID rootAsset =
			database.ImportOrGet("game://Tests/Root.scene.json", Engine::AssetType::Scene);

		Engine::ECSWorld world;
		Engine::SceneSystem sceneSystem;
		Engine::SceneInstanceManager scenes;
		bool passed = scenes.LoadSceneTree(database, sceneSystem, world, rootAsset) &&
			scenes.GetAll().size() == 2;
		const Engine::SceneInstance* active = scenes.GetActive();
		const Engine::UUID rootInstanceID = active ? active->instanceID : Engine::UUID{};

		Engine::UUID childInstanceID{};
		if (active && !active->childScenes.empty()) {
			childInstanceID = active->childScenes.front().childInstanceID;
		}
		Engine::SceneInstance* editableRoot = active ?
			scenes.Find(active->instanceID) : nullptr;
		if (editableRoot && !editableRoot->header.subScenes.empty()) {
			editableRoot->header.subScenes.front().slotName = "RenamedChild";
			passed &= scenes.SynchronizeSubScenes(
				database, sceneSystem, world, editableRoot->instanceID);
		} else {
			passed = false;
		}
		editableRoot = scenes.GetActive() ?
			scenes.Find(scenes.GetActive()->instanceID) : nullptr;
		passed &= editableRoot && !editableRoot->childScenes.empty() &&
			editableRoot->childScenes.front().childInstanceID == childInstanceID &&
			editableRoot->childScenes.front().slotName == "RenamedChild";

		passed &= rootInstanceID && scenes.Unload(world, rootInstanceID) && scenes.GetAll().empty();

		childHeader.subScenes.push_back({
			.slotID = Engine::UUID{ 102 },
			.slotName = "Root",
			.sceneAsset = rootAsset,
			.enabled = true,
			});
		passed &= SaveScene(childPath, childHeader);
		passed &= !scenes.LoadSceneTree(database, sceneSystem, world, rootAsset);
		passed &= scenes.GetAll().empty();
		size_t aliveCount = 0;
		world.ForEachAliveEntity([&aliveCount](Engine::Entity) { ++aliveCount; });
		passed &= aliveCount == 0;

		std::filesystem::remove_all(testRoot, ec);
		return passed && !ec;
	}

	bool TestSingleSceneLoadReservation() {

		const std::filesystem::path testRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() / "Tests/SingleSceneLoad";
		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}
		Engine::AssetDatabase database;
		database.Init();
		std::array<Engine::AssetID, 3> sceneAssets{};
		for (size_t i = 0; i < sceneAssets.size(); ++i) {

			Engine::SceneHeader header{};
			header.name = "Single" + std::to_string(i + 1);
			const std::filesystem::path path = testRoot /
				(header.name + ".scene.json");
			const nlohmann::json root = {
				{ "SchemaVersion", 3 },
				{ "Header", Engine::ToJson(header) },
				{ "ExternalActors", nlohmann::json::array() },
				{ "PrefabInstances", nlohmann::json::array() },
			};
			if (!Engine::JsonAdapter::SaveCanonical(path, root)) {
				std::filesystem::remove_all(testRoot, ec);
				return false;
			}
			sceneAssets[i] = database.ImportOrGet(
				Engine::RuntimePaths::ToAssetPath(path), Engine::AssetType::Scene);
			if (!sceneAssets[i]) {
				std::filesystem::remove_all(testRoot, ec);
				return false;
			}
		}

		Engine::ECSWorld world;
		Engine::SceneSystem sceneSystem;
		Engine::SceneInstanceManager scenes;
		Engine::WorldCommandServices services{};
		services.sceneInstances = &scenes;
		world.SetCommandServices(services);

		// Scene用Service不足で処理できない場合も予約を残さない
		bool passed = scenes.TryBeginSingleLoadRequest();
		world.GetCommandBuffer().EnqueueLoadSceneSingle(Engine::UUID::New(), sceneAssets.front());
		world.GetCommandBuffer().Flush(world);
		passed &= scenes.TryBeginSingleLoadRequest();
		scenes.ClearSingleLoadRequest();

		services.assetDatabase = &database;
		services.sceneSystem = &sceneSystem;
		world.SetCommandServices(services);
		for (const Engine::AssetID sceneAsset : sceneAssets) {

			if (!scenes.TryBeginSingleLoadRequest()) {
				passed = false;
				break;
			}
			const Engine::UUID instanceID = Engine::UUID::New();
			world.GetCommandBuffer().EnqueueLoadSceneSingle(instanceID, sceneAsset);
			world.GetCommandBuffer().Flush(world);
			const Engine::SceneInstance* active = scenes.GetActive();
			passed &= active && active->instanceID == instanceID &&
				active->sceneAsset == sceneAsset && scenes.GetAll().size() == 1;
		}
		passed &= scenes.TryBeginSingleLoadRequest();
		scenes.ClearSingleLoadRequest();

		// 常駐化は親子の実体と音声Runtimeを保持し、Singleの破棄対象から外す
		if (!passed || !scenes.GetActive()) {
			std::filesystem::remove_all(testRoot, ec);
			return false;
		}
		const Engine::Entity music = Engine::SceneAuthoring::CreateGameObject(world, "Music");
		const Engine::Entity source = Engine::SceneAuthoring::CreateGameObject(world, "Source");
		Engine::HierarchySystem hierarchy;
		hierarchy.SetParent(world, source, music);
		const Engine::UUID owner = scenes.GetActive()->instanceID;
		for (Engine::Entity entity : { music, source }) {
			world.GetComponent<Engine::SceneObjectComponent>(entity).sceneInstanceID = owner;
			scenes.Find(owner)->createdEntities.emplace_back(entity);
		}
		world.AddComponent<Engine::AudioSourceComponent>(source);
		const auto* audioRuntime = Engine::TryGetAudioSourceRuntime(world, source);
		passed &= audioRuntime != nullptr;
		passed &= !scenes.DontDestroyOnLoad(world, source);
		passed &= scenes.DontDestroyOnLoad(world, music);
		passed &= scenes.DontDestroyOnLoad(world, music);
		const Engine::UUID persistentID = world.GetComponent<Engine::SceneObjectComponent>(music).sceneInstanceID;
		passed &= persistentID != owner && scenes.Find(persistentID)->persistent;
		passed &= scenes.Find(owner)->createdEntities.empty();
		passed &= !scenes.Unload(world, persistentID);
		scenes.SetActive(persistentID);
		passed &= scenes.GetActive()->instanceID == owner;
		for (Engine::AssetID asset : sceneAssets) {
			passed &= scenes.TryBeginSingleLoadRequest();
			world.GetCommandBuffer().EnqueueLoadSceneSingle(Engine::UUID::New(), asset);
			world.GetCommandBuffer().Flush(world);
			passed &= world.IsAlive(music) && world.IsAlive(source) && scenes.GetAll().size() == 2;
			passed &= Engine::TryGetAudioSourceRuntime(world, source) == audioRuntime;
		}
		passed &= scenes.SerializeSnapshot(sceneSystem, world)["Scenes"].size() == 1;
		world.GetCommandBuffer().EnqueueDestroyEntity(music);
		world.GetCommandBuffer().Flush(world);
		world.FlushPendingDestroyEntities();
		passed &= !world.IsAlive(music) && !world.IsAlive(source);
		passed &= !scenes.DontDestroyOnLoad(world, music);
		const Engine::Entity second = Engine::SceneAuthoring::CreateGameObject(world, "Second");
		passed &= scenes.DontDestroyOnLoad(world, second);
		scenes.UnloadAll(world);
		passed &= !world.IsAlive(second) && scenes.GetAll().empty();

		std::filesystem::remove_all(testRoot, ec);
		return passed && !ec;
	}
}
