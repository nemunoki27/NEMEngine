#include "TestContracts.h"
#include "TestFixtures.h"

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
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Runtime/SceneInstanceManager.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
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

	bool TestSceneAssetCopy() {

		TestDirectory directory("SceneCopy", Engine::RuntimePaths::GetGameAssetsRoot());
		const auto& testRoot = directory.GetPath();
		std::filesystem::create_directories(testRoot / "Folder");
		Engine::ECSWorld world;
		const Engine::Entity parent = Engine::SceneAuthoring::CreateGameObject(world, "Parent");
		const Engine::Entity child = Engine::SceneAuthoring::CreateGameObject(world, "Child");
		Engine::HierarchySystem hierarchy;
		hierarchy.SetParent(world, child, parent);
		const Engine::UUID parentID = world.GetComponent<Engine::SceneObjectComponent>(parent).localFileID;
		const Engine::UUID childID = world.GetComponent<Engine::SceneObjectComponent>(child).localFileID;
		Engine::SceneSystem system;
		std::vector<std::filesystem::path> actorRoots;
		bool passed = true;
		std::string error;

		// 両方の保存形式で参照と階層を維持した独立コピーを確認する
		for (bool external : { false, true }) {

			const std::filesystem::path source = testRoot / (external ? "External.scene.json" : "Single.scene.json");
			const std::filesystem::path target = testRoot / "Folder" / source.filename();
			Engine::AssetMeta meta{};
			meta.guid = Engine::AssetGUID::New();
			meta.type = Engine::AssetType::Scene;
			const Engine::AssetID otherAsset = Engine::AssetGUID::New();
			const nlohmann::json selfReference = {
				{ "kind", "Scene" }, { "sourceAsset", Engine::ToString(meta.guid) },
				{ "localFileId", Engine::ToString(childID) },
			};
			nlohmann::json otherReference = selfReference;
			otherReference["sourceAsset"] = Engine::ToString(otherAsset);
			nlohmann::json implicitReference = selfReference;
			implicitReference["sourceAsset"] = "";
			nlohmann::json prefabReference = selfReference;
			prefabReference["kind"] = "Prefab";
			nlohmann::json root = {
				{ "SchemaVersion", 3 }, { "Header", Engine::ToJson(Engine::SceneHeader{}) },
				{ "Entities", system.SerializeEntities(world) },
				{ "PrefabInstances", nlohmann::json::array() },
				{ "CopyReferences", { selfReference, otherReference, implicitReference, prefabReference } },
			};
			// 任意のシリアライズ領域でも型付き参照だけを書き換える
			root["Entities"][0]["CopyReference"] = selfReference;
			root["Header"]["sharedAsset"] = Engine::ToString(meta.guid);
			passed &= Engine::AssetDatabase::WriteMetaFile(source.wstring() + L".meta", meta) &&
				Engine::SceneSystem::WriteSaveSnapshot({ source, meta.guid, root, external });
			const std::filesystem::path sourceActors = Engine::RuntimePaths::GetGameAssetsRoot() /
				"ExternalActors" / Engine::ToString(meta.guid);
			actorRoots.push_back(sourceActors);
			const nlohmann::json sourceBefore = Engine::JsonAdapter::Load(source, false);
			passed &= Engine::SceneSystem::CopySceneAssets({ { source, target } }, error);
			Engine::AssetMeta copiedMeta{};
			passed &= Engine::AssetDatabase::ReadMetaFile(target.wstring() + L".meta", copiedMeta) &&
				copiedMeta.guid && copiedMeta.guid != meta.guid;
			const std::filesystem::path copiedActors = Engine::RuntimePaths::GetGameAssetsRoot() /
				"ExternalActors" / Engine::ToString(copiedMeta.guid);
			actorRoots.push_back(copiedActors);
			const nlohmann::json copied = Engine::JsonAdapter::Load(target, false);
			if (!copied.is_object()) {
				passed = false;
				break;
			}
			passed &= copied.contains("ExternalActors") == external &&
				copied["CopyReferences"][0]["sourceAsset"] == Engine::ToString(copiedMeta.guid) &&
				copied["CopyReferences"][1] == otherReference &&
				copied["CopyReferences"][2] == implicitReference &&
				copied["CopyReferences"][3] == prefabReference &&
				copied["Header"]["sharedAsset"] == Engine::ToString(meta.guid);
			nlohmann::json expected = sourceBefore;
			expected["Header"]["name"] = external ? "External" : "Single";
			expected["CopyReferences"][0]["sourceAsset"] = Engine::ToString(copiedMeta.guid);
			if (!external) {
				expected["Entities"][0]["CopyReference"]["sourceAsset"] = Engine::ToString(copiedMeta.guid);
			}
			passed &= copied == expected;
			Engine::ECSWorld loaded;
			std::vector<Engine::Entity> entities;
			passed &= system.LoadScene(target, loaded, nullptr, copiedMeta.guid,
				Engine::UUID{ 500 }, nullptr, &entities) && entities.size() == 2;
			bool childFound = false;
			for (Engine::Entity entity : entities) {

				if (loaded.GetComponent<Engine::SceneObjectComponent>(entity).localFileID == childID) {
					childFound = loaded.GetComponent<Engine::HierarchyComponent>(entity).parentLocalFileID == parentID;
				}
			}
			passed &= childFound;
			passed &= !Engine::SceneSystem::CopySceneAssets({ { source, target } }, error) &&
				Engine::JsonAdapter::Load(target, false) == copied;
			if (external) {

				const std::string actorName = copied["ExternalActors"][0].get<std::string>() + ".actor.json";
				nlohmann::json copiedActor = Engine::JsonAdapter::Load(copiedActors / actorName, false);
				nlohmann::json expectedActor = Engine::JsonAdapter::Load(sourceActors / actorName, false);
				const nlohmann::json originalActor = expectedActor;
				expectedActor["CopyReference"]["sourceAsset"] = Engine::ToString(copiedMeta.guid);
				passed &= copiedActor == expectedActor;
				copiedActor["Components"]["Name"]["name"] = "Changed";
				passed &= Engine::JsonAdapter::SaveCanonical(copiedActors / actorName, copiedActor) &&
					Engine::JsonAdapter::Load(sourceActors / actorName, false) == originalActor;
				// Actor欠損時はバッチ全体を作成しない
				std::filesystem::remove(sourceActors / actorName);
				const std::filesystem::path rejected = testRoot / "Rejected.scene.json";
				const std::filesystem::path first = testRoot / "First.scene.json";
				passed &= !Engine::SceneSystem::CopySceneAssets({
					{ testRoot / "Single.scene.json", first }, { source, rejected } }, error) &&
					!std::filesystem::exists(first) && !std::filesystem::exists(rejected) &&
					!std::filesystem::exists(rejected.wstring() + L".meta");
			}
			passed &= Engine::JsonAdapter::Load(source, false) == sourceBefore;
		}
		// 空シーンと日本語ファイル名も同じ形式で複製する
		for (bool external : { false, true }) {

			const auto source = testRoot / (external ? L"空の外部.scene.json" : L"空の単一.scene.json");
			const auto target = testRoot / "Folder" / source.filename();
			Engine::AssetMeta meta{};
			meta.type = Engine::AssetType::Scene;
			meta.guid = Engine::AssetGUID::New();
			const nlohmann::json root = {
				{ "SchemaVersion", 3 }, { "Header", Engine::ToJson(Engine::SceneHeader{}) },
				{ "Entities", nlohmann::json::array() }, { "PrefabInstances", nlohmann::json::array() },
			};
			passed &= Engine::AssetDatabase::WriteMetaFile(source.wstring() + L".meta", meta) &&
				Engine::SceneSystem::WriteSaveSnapshot({ source, meta.guid, root, external }) &&
				Engine::SceneSystem::CopySceneAssets({ { source, target } }, error);
			Engine::AssetMeta copiedMeta{};
			passed &= Engine::AssetDatabase::ReadMetaFile(target.wstring() + L".meta", copiedMeta);
			Engine::ECSWorld loaded;
			std::vector<Engine::Entity> created;
			passed &= system.LoadScene(target, loaded, nullptr, copiedMeta.guid,
				Engine::UUID{ 501 }, nullptr, &created) && created.empty() &&
				Engine::JsonAdapter::Load(target, false).contains("ExternalActors") == external;
			// JSONの型が壊れている場合も例外を外へ出さず複製を中止する
			nlohmann::json invalid = root;
			invalid["SchemaVersion"] = "invalid";
			const auto rejected = testRoot / "Invalid.scene.json";
			passed &= Engine::JsonAdapter::SaveCanonical(source, invalid) &&
				!Engine::SceneSystem::CopySceneAssets({ { source, rejected } }, error) &&
				!std::filesystem::exists(rejected) && !std::filesystem::exists(rejected.wstring() + L".meta");
			actorRoots.push_back(Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" / Engine::ToString(meta.guid));
			actorRoots.push_back(Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors" / Engine::ToString(copiedMeta.guid));
		}
		std::error_code ec;
		directory.Remove();
		for (const auto& actorRoot : actorRoots) {

			std::filesystem::remove_all(actorRoot, ec);
		}
		return passed;
	}

	bool TestSceneLifecycleContext() {

		Engine::ECSWorld world;
		Engine::AssetDatabase database;
		Engine::SceneSystem sceneSystem;
		Engine::SceneInstanceManager scenes;
		Engine::SceneHeader firstHeader{};
		firstHeader.name = "First";
		const Engine::UUID firstScene = scenes.CreateScratchScene(firstHeader);
		Engine::SceneHeader secondHeader{};
		secondHeader.name = "Second";
		const Engine::UUID secondScene = scenes.CreateScratchScene(secondHeader);
		scenes.SetActive(firstScene);

		Engine::WorldCommandServices services{};
		services.assetDatabase = &database;
		services.sceneInstances = &scenes;
		services.sceneSystem = &sceneSystem;
		world.SetCommandServices(services);

		Engine::SystemContext context{};
		context.world = &world;
		context.assetDatabase = &database;
		context.mode = Engine::WorldMode::Play;
		context.activeSceneHeader = &scenes.GetActive()->header;

		auto observer = std::make_unique<SceneContextObserverSystem>();
		SceneContextObserverSystem* observerPtr = observer.get();
		Engine::SystemScheduler scheduler;
		scheduler.AddSystem(std::move(observer), 0);
		scheduler.Tick(&world, context);

		world.GetCommandBuffer().EnqueueUnloadScene(firstScene);
		scheduler.Tick(&world, context);

		const Engine::SceneInstance* activeScene = scenes.GetActive();
		return activeScene && activeScene->instanceID == secondScene &&
			context.activeSceneHeader == &activeScene->header &&
			observerPtr->observedHeader == &activeScene->header &&
			context.activeSceneHeader->name == "Second" && observerPtr->notificationCount == 1;
	}
}
