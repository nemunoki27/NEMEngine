#include "TestContracts.h"
#include "TestFixtures.h"
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneAssetStorage.h>

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
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/UI/CanvasComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

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

	bool TestPrefabPropagationAndNestedInstances() {

		Engine::RuntimePaths::Refresh();
		TestDirectory directory("PrefabPropagation", Engine::RuntimePaths::GetGameAssetsRoot());
		const auto& testRoot = directory.GetPath();
		std::error_code ec;
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		Engine::AssetDatabase database;
		database.Init();
		Engine::HierarchySystem hierarchySystem;
		Engine::PrefabSystem prefabSystem;
		const std::string nestedPath =
			Engine::RuntimePaths::ToAssetPath(testRoot / "Nested.prefab.json");
		const std::string outerPath =
			Engine::RuntimePaths::ToAssetPath(testRoot / "Outer.prefab.json");

		Engine::ECSWorld nestedSourceWorld;
		const Engine::Entity nestedSourceRoot =
			Engine::SceneAuthoring::CreateGameObject(nestedSourceWorld, "NestedRoot");
		const Engine::Entity nestedSourceChild =
			Engine::SceneAuthoring::CreateGameObject(nestedSourceWorld, "NestedChild");
		hierarchySystem.SetParent(nestedSourceWorld, nestedSourceChild, nestedSourceRoot);
		bool passed = prefabSystem.SavePrefab(
			database, nestedSourceWorld, nestedSourceRoot, nestedPath);
		const Engine::AssetID nestedAsset = database.ImportOrGet(
			nestedPath, Engine::AssetType::Prefab);

		Engine::ECSWorld outerSourceWorld;
		const Engine::Entity outerSourceRoot =
			Engine::SceneAuthoring::CreateGameObject(outerSourceWorld, "OuterRoot");
		const Engine::Entity outerSourceChild =
			Engine::SceneAuthoring::CreateGameObject(outerSourceWorld, "OuterChild");
		hierarchySystem.SetParent(outerSourceWorld, outerSourceChild, outerSourceRoot);
		Engine::PrefabInstantiateDesc nestedDesc{};
		nestedDesc.parent = outerSourceChild;
		Engine::PrefabInstantiateResult nestedSourceResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, outerSourceWorld, nestedAsset, nestedSourceResult, nestedDesc);
		Engine::PrefabInstantiateResult secondNestedSourceResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, outerSourceWorld, nestedAsset, secondNestedSourceResult, nestedDesc);
		auto& sourceCanvas = outerSourceWorld.AddComponent<Engine::CanvasComponent>(outerSourceRoot);
		sourceCanvas.navigationRows = 1;
		sourceCanvas.navigationColumns = 2;
		sourceCanvas.firstSelectedLocalFileID =
			outerSourceWorld.GetComponent<Engine::SceneObjectComponent>(nestedSourceResult.root).localFileID;
		const std::array<Engine::UUID, 2> sourceNavigation = {
			sourceCanvas.firstSelectedLocalFileID,
			outerSourceWorld.GetComponent<Engine::SceneObjectComponent>(secondNestedSourceResult.root).localFileID
		};
		Engine::SetCanvasNavigationCells(outerSourceWorld, outerSourceRoot, sourceNavigation);
		const Engine::UUID outerSourceInstanceID = Engine::UUID::New();
		passed &= prefabSystem.SavePrefab(
			database, outerSourceWorld, outerSourceRoot, outerPath, outerSourceInstanceID);
		const Engine::AssetID outerAsset = database.ImportOrGet(
			outerPath, Engine::AssetType::Prefab);
		prefabSystem.SetPrefabLinkToSubtree(
			outerSourceWorld, outerSourceRoot, outerAsset, outerSourceInstanceID);

		const nlohmann::json outerJson = Engine::JsonAdapter::Load(
			database.ResolveFullPath(outerAsset));
		passed &= outerJson.is_object() && outerJson.value("SchemaVersion", 0u) == 2u &&
			outerJson.contains("Entities") && outerJson["Entities"].size() == 2 &&
			outerJson.contains("NestedPrefabInstances") &&
			outerJson["NestedPrefabInstances"].size() == 2;
		if (!passed) { std::cerr << "Nested prefab save failed\n"; return false; }

		Engine::ECSWorld targetWorld;
		const Engine::Entity externalParent =
			Engine::SceneAuthoring::CreateGameObject(targetWorld, "ExternalParent");
		Engine::PrefabInstantiateDesc outerDesc{};
		outerDesc.parent = externalParent;
		Engine::PrefabInstantiateResult outerResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, targetWorld, outerAsset, outerResult, outerDesc);

		Engine::Entity nestedTargetRoot = Engine::Entity::Null();
		std::vector<Engine::UUID> nestedTargetLocalFileIDs;
		targetWorld.ForEach<Engine::PrefabLinkComponent>(
			[&](const Engine::Entity& entity, Engine::PrefabLinkComponent& link) {

				if (link.prefabAsset == nestedAsset && link.isPrefabRoot &&
					link.ownerPrefabInstanceID == outerResult.prefabInstanceID) {
					nestedTargetRoot = entity;
					nestedTargetLocalFileIDs.emplace_back(
						targetWorld.GetComponent<Engine::SceneObjectComponent>(entity).localFileID);
				}
			});
		const Engine::CanvasComponent* targetCanvas =
			targetWorld.TryGetComponent<Engine::CanvasComponent>(outerResult.root);
		const std::span<const Engine::CanvasNavigationCell> targetNavigation =
			Engine::GetCanvasNavigationCells(targetWorld, outerResult.root);
		passed &= targetCanvas && nestedTargetLocalFileIDs.size() == 2 &&
			nestedTargetLocalFileIDs[0] != nestedTargetLocalFileIDs[1] &&
			std::find(nestedTargetLocalFileIDs.begin(), nestedTargetLocalFileIDs.end(),
				targetCanvas->firstSelectedLocalFileID) != nestedTargetLocalFileIDs.end() &&
			targetNavigation.size() == 2 &&
			targetNavigation[0].localFileID != targetNavigation[1].localFileID &&
			std::find(nestedTargetLocalFileIDs.begin(), nestedTargetLocalFileIDs.end(),
				targetNavigation[0].localFileID) != nestedTargetLocalFileIDs.end() &&
			std::find(nestedTargetLocalFileIDs.begin(), nestedTargetLocalFileIDs.end(),
				targetNavigation[1].localFileID) != nestedTargetLocalFileIDs.end();
		if (!passed) { std::cerr << "Nested prefab references failed\n"; return false; }
		const Engine::Entity addedChild =
			Engine::SceneAuthoring::CreateGameObject(targetWorld, "AddedChild");
		hierarchySystem.SetParent(targetWorld, addedChild, outerResult.root);

		const Engine::UUID outerStableUUID = targetWorld.IsAlive(outerResult.root) ?
			targetWorld.GetUUID(outerResult.root) : Engine::UUID{};
		const Engine::UUID nestedStableUUID = targetWorld.IsAlive(nestedTargetRoot) ?
			targetWorld.GetUUID(nestedTargetRoot) : Engine::UUID{};
		const Engine::UUID addedStableUUID = targetWorld.IsAlive(addedChild) ?
			targetWorld.GetUUID(addedChild) : Engine::UUID{};
		const Engine::HierarchyComponent* nestedHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(nestedTargetRoot);
		passed &= targetWorld.IsAlive(nestedTargetRoot) && nestedHierarchy &&
			nestedHierarchy->parent.IsValid();

		const auto oldNestedBase = Engine::PrefabOverrideUtility::LoadPrefabBaseEntities(
			database, nestedAsset);
		nestedSourceWorld.GetComponent<Engine::NameComponent>(nestedSourceChild).name =
			"NestedChildUpdated";
		passed &= prefabSystem.SavePrefab(
			database, nestedSourceWorld, nestedSourceRoot, nestedPath);
		passed &= Engine::PrefabOverrideUtility::PropagateToInstances(
			targetWorld, database, hierarchySystem, nestedAsset, oldNestedBase);
		passed &= Engine::PrefabOverrideUtility::PropagateToInstances(
			outerSourceWorld, database, hierarchySystem, nestedAsset, oldNestedBase);
		nestedTargetRoot = targetWorld.FindByUUID(nestedStableUUID);
		nestedHierarchy = targetWorld.TryGetComponent<Engine::HierarchyComponent>(nestedTargetRoot);
		passed &= targetWorld.IsAlive(nestedTargetRoot) && nestedHierarchy &&
			targetWorld.IsAlive(nestedHierarchy->parent);

		const auto oldBase = Engine::PrefabOverrideUtility::LoadPrefabBaseEntities(
			database, outerAsset);
		outerSourceWorld.GetComponent<Engine::NameComponent>(outerSourceChild).name = "OuterChildUpdated";
		passed &= prefabSystem.SavePrefab(
			database, outerSourceWorld, outerSourceRoot, outerPath);
		passed &= Engine::PrefabOverrideUtility::PropagateToInstances(
			targetWorld, database, hierarchySystem, outerAsset, oldBase);

		const Engine::Entity rebuiltRoot = targetWorld.FindByUUID(outerStableUUID);
		const Engine::Entity rebuiltNestedRoot = targetWorld.FindByUUID(nestedStableUUID);
		const Engine::Entity rebuiltAddedChild = targetWorld.FindByUUID(addedStableUUID);
		const Engine::HierarchyComponent* rebuiltRootHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(rebuiltRoot);
		const Engine::HierarchyComponent* externalHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(externalParent);
		passed &= targetWorld.IsAlive(rebuiltRoot) && targetWorld.IsAlive(rebuiltNestedRoot) &&
			targetWorld.IsAlive(rebuiltAddedChild) && rebuiltRootHierarchy &&
			rebuiltRootHierarchy->parent == externalParent && externalHierarchy &&
			externalHierarchy->firstChild == rebuiltRoot;

		bool updatedChildFound = false;
		for (const Engine::Entity& entity :
			Engine::HierarchyUtility::CollectLogicalSubtree(targetWorld, rebuiltRoot)) {

			if (targetWorld.HasComponent<Engine::NameComponent>(entity) &&
				targetWorld.GetComponent<Engine::NameComponent>(entity).name == "OuterChildUpdated") {
				updatedChildFound = true;
			}
		}
		passed &= updatedChildFound;
		if (!passed) { std::cerr << "Nested prefab propagation failed\n"; return false; }

		const std::filesystem::path scenePath = testRoot / "NestedRoundTrip.scene.json";
		Engine::SceneHeader sceneHeader{};
		sceneHeader.name = "NestedRoundTrip";
		sceneHeader.guid = Engine::AssetGUID::New();
		Engine::AssetMeta sceneMeta{};
		sceneMeta.guid = sceneHeader.guid;
		sceneMeta.type = Engine::AssetType::Scene;
		passed &= Engine::AssetDatabase::WriteMetaFile(scenePath.wstring() + L".meta", sceneMeta);
		Engine::SceneSystem sceneSystem;
		Engine::SceneSaveSnapshot sceneSnapshot{};
		passed &= sceneSystem.CaptureSaveSnapshot(
			scenePath, targetWorld, sceneHeader, database, sceneSnapshot);
		if (passed) {
			sceneSnapshot.useExternalActors = false;
			passed &= Engine::SceneSystem::WriteSaveSnapshot(std::move(sceneSnapshot));
		}
		Engine::ECSWorld loadedSceneWorld;
		std::vector<Engine::Entity> loadedSceneEntities;
		passed &= sceneSystem.LoadScene(
			scenePath, loadedSceneWorld, &database, Engine::AssetID{},
			Engine::UUID{ 700 }, nullptr, &loadedSceneEntities);
		// 入れ子Prefabと追加エンティティを含むシーンも両形式で複製できる
		for (bool external : { false, true }) {

			Engine::SceneSaveSnapshot copySource{};
			passed &= sceneSystem.CaptureSaveSnapshot(
				scenePath, targetWorld, sceneHeader, database, copySource);
			copySource.useExternalActors = external;
			passed &= Engine::SceneSystem::WriteSaveSnapshot(copySource);
			const auto copyPath = testRoot / (external ? "NestedCopyExternal.scene.json" : "NestedCopy.scene.json");
			std::string copyError;
			if (!Engine::SceneSystem::CopySceneAssets({ { scenePath, copyPath } }, copyError)) {

				passed = false;
				break;
			}
			const Engine::AssetID copyAsset = database.ImportOrGet(
				Engine::RuntimePaths::ToAssetPath(copyPath), Engine::AssetType::Scene);
			Engine::ECSWorld copyWorld;
			std::vector<Engine::Entity> copyEntities;
			passed &= sceneSystem.LoadScene(copyPath, copyWorld, &database, copyAsset,
				Engine::UUID{ 701 }, nullptr, &copyEntities) && copyEntities.size() == loadedSceneEntities.size();
			bool copyNested = false;
			bool copyAdded = false;
			copyWorld.ForEachAliveEntity([&](Engine::Entity entity) {

				const auto* link = copyWorld.TryGetComponent<Engine::PrefabLinkComponent>(entity);
				copyNested |= link && link->prefabAsset == nestedAsset;
				const auto& sceneObject = copyWorld.GetComponent<Engine::SceneObjectComponent>(entity);
				copyAdded |= sceneObject.localFileID ==
					targetWorld.GetComponent<Engine::SceneObjectComponent>(rebuiltAddedChild).localFileID;
				});
			passed &= copyNested && copyAdded;
			const auto actors = Engine::RuntimePaths::GetGameAssetsRoot() / "ExternalActors";
			std::filesystem::remove_all(actors / Engine::ToString(copyAsset), ec);
			std::filesystem::remove_all(actors / Engine::ToString(copySource.sceneAsset), ec);
		}
		Engine::UUID loadedOuterInstanceID{};
		bool loadedNestedInstance = false;
		loadedSceneWorld.ForEach<Engine::PrefabLinkComponent>(
			[&](const Engine::Entity&, Engine::PrefabLinkComponent& link) {

				if (link.prefabAsset == outerAsset && link.isPrefabRoot &&
					!link.ownerPrefabInstanceID) {
					loadedOuterInstanceID = link.prefabInstanceID;
				}
			});
		loadedSceneWorld.ForEach<Engine::PrefabLinkComponent>(
			[&](const Engine::Entity&, Engine::PrefabLinkComponent& link) {

				if (link.prefabAsset == nestedAsset && link.isPrefabRoot &&
					link.ownerPrefabInstanceID == loadedOuterInstanceID) {
					loadedNestedInstance = true;
				}
			});
		passed &= loadedOuterInstanceID && loadedNestedInstance;

		const std::vector<Engine::Entity> nestedSubtree =
			Engine::HierarchyUtility::CollectLogicalSubtree(targetWorld, rebuiltNestedRoot);
		for (auto it = nestedSubtree.rbegin(); it != nestedSubtree.rend(); ++it) {

			if (targetWorld.IsAlive(*it)) {
				targetWorld.DestroyEntity(*it);
			}
		}
		targetWorld.FlushPendingDestroyEntities();
		std::vector<Engine::Entity> hierarchyScope;
		targetWorld.ForEachAliveEntity([&](Engine::Entity entity) {
			hierarchyScope.emplace_back(entity);
			});
		hierarchySystem.RebuildRuntimeLinks(targetWorld, hierarchyScope);

		const auto currentBase = Engine::PrefabOverrideUtility::LoadPrefabBaseEntities(
			database, outerAsset);
		Engine::PrefabInstanceData removedNestedData =
			Engine::PrefabOverrideUtility::CaptureInstance(
				targetWorld, database, outerResult.prefabInstanceID, currentBase);
		passed &= removedNestedData.removedNestedSlots.size() == 1;

		Engine::ECSWorld restoredWorld;
		const Engine::Entity restoredParent =
			Engine::SceneAuthoring::CreateGameObject(restoredWorld, "RestoredParent");
		restoredWorld.GetComponent<Engine::SceneObjectComponent>(restoredParent).localFileID =
			targetWorld.GetComponent<Engine::SceneObjectComponent>(externalParent).localFileID;
		const Engine::Entity restoredRoot = Engine::PrefabOverrideUtility::RebuildInstance(
			restoredWorld, database, hierarchySystem, removedNestedData, Engine::UUID{});
		size_t nestedRestored = 0;
		bool removedSlotRestored = false;
		restoredWorld.ForEach<Engine::PrefabLinkComponent>(
			[&](const Engine::Entity&, Engine::PrefabLinkComponent& link) {
				if (link.ownerPrefabInstanceID == removedNestedData.instanceID && link.isPrefabRoot) {
					++nestedRestored;
					removedSlotRestored |= std::find(removedNestedData.removedNestedSlots.begin(),
						removedNestedData.removedNestedSlots.end(), link.nestedSlotID) != removedNestedData.removedNestedSlots.end();
				}
			});
		passed &= restoredWorld.IsAlive(restoredRoot) && nestedRestored == 1 && !removedSlotRestored;

		Engine::ECSWorld unpackWorld;
		const Engine::Entity unpackParent =
			Engine::SceneAuthoring::CreateGameObject(unpackWorld, "UnpackParent");
		Engine::PrefabInstantiateDesc unpackDesc{};
		unpackDesc.parent = unpackParent;
		Engine::PrefabInstantiateResult unpackResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, unpackWorld, outerAsset, unpackResult, unpackDesc);
		unpackWorld.GetComponent<Engine::NameComponent>(unpackResult.root).name = "OverrideName";
		passed &= prefabSystem.UnpackPrefabInstance(
			unpackWorld, unpackResult.root, Engine::PrefabUnpackMode::OutermostRoot);
		bool outerLinkRemains = false;
		bool nestedLinkRemains = false;
		bool nestedOwnerRemains = false;
		unpackWorld.ForEach<Engine::PrefabLinkComponent>(
			[&](const Engine::Entity&, Engine::PrefabLinkComponent& link) {

				outerLinkRemains |= link.prefabInstanceID == unpackResult.prefabInstanceID;
				if (link.prefabAsset == nestedAsset) {
					nestedLinkRemains = true;
					nestedOwnerRemains |= static_cast<bool>(link.ownerPrefabInstanceID);
				}
			});
		const Engine::HierarchyComponent* unpackHierarchy =
			unpackWorld.TryGetComponent<Engine::HierarchyComponent>(unpackResult.root);
		passed &= !outerLinkRemains && nestedLinkRemains && !nestedOwnerRemains &&
			unpackWorld.GetComponent<Engine::NameComponent>(unpackResult.root).name == "OverrideName" &&
			unpackHierarchy && unpackHierarchy->parent == unpackParent;

		Engine::ECSWorld completeWorld;
		Engine::PrefabInstantiateResult completeResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, completeWorld, outerAsset, completeResult);
		passed &= prefabSystem.UnpackPrefabInstance(
			completeWorld, completeResult.root, Engine::PrefabUnpackMode::Completely);
		bool completeLinkRemains = false;
		for (const Engine::Entity& entity :
			Engine::HierarchyUtility::CollectLogicalSubtree(completeWorld, completeResult.root)) {

			completeLinkRemains |= completeWorld.HasComponent<Engine::PrefabLinkComponent>(entity);
		}
		passed &= !completeLinkRemains;

		const std::string invalidPath =
			Engine::RuntimePaths::ToAssetPath(testRoot / "Invalid.prefab.json");
		passed &= Engine::JsonAdapter::SaveCanonical(
			database.ResolveAssetPath(invalidPath), nlohmann::json::object());
		const Engine::AssetID invalidAsset = database.ImportOrGet(
			invalidPath, Engine::AssetType::Prefab);
		size_t entityCount = 0;
		targetWorld.ForEachAliveEntity([&](const Engine::Entity&) {
			++entityCount;
			});
		Engine::PrefabInstantiateResult invalidResult{};
		passed &= !prefabSystem.InstantiatePrefab(
			database, hierarchySystem, targetWorld, invalidAsset, invalidResult);
		size_t entityCountAfterFailure = 0;
		targetWorld.ForEachAliveEntity([&](const Engine::Entity&) {
			++entityCountAfterFailure;
			});
		passed &= entityCount == entityCountAfterFailure;

		directory.Remove();
		return passed && !ec;
	}
}
