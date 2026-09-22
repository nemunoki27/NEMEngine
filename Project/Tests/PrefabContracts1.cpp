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
#include <Engine/Core/Rendering/RenderFeatures/RenderFeatureProfileSerializer.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
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

	bool TestPrefabImmediateHierarchy() {

		// 旧対応表は先頭を維持し、参照先と参照空間を混同しない
		{
			using namespace Engine;
			PrefabInstanceData instance;
			using Engine::UUID;
			instance.prefabAsset = AssetID{ 1, 2 };
			instance.instanceID = UUID{ 1 };
			instance.entityMap = { { UUID{ 10 }, UUID{ 20 } }, { UUID{ 10 }, UUID{ 21 } },
				{ UUID{ 11 }, UUID{ 22 } }, { UUID{ 11 }, UUID{ 23 } },
				{ UUID{ 12 }, UUID{ 24 } }, { UUID{ 12 }, UUID{ 25 } } };
			const auto sceneRef = nlohmann::json{ { "kind", "Scene" }, { "sourceAsset", "" },
				{ "localFileId", ToString(UUID{ 21 }) } };
			auto prefabRef = sceneRef;
			prefabRef["kind"] = "Prefab";
			auto foreignRef = sceneRef;
			foreignRef["sourceAsset"] = ToString(AssetID{ 9, 9 });
			nlohmann::json scene = {
				{ "Entities", nlohmann::json::array({ {
					{ "LocalFileID", ToString(UUID{ 30 }) },
					{ "Components", { { "Hierarchy", { { "parentLocalFileID", ToString(UUID{ 23 }) } } },
						{ "ScriptComponent", { { "sceneRef", sceneRef }, { "prefabRef", prefabRef },
							{ "foreignRef", foreignRef }, { "text", ToString(UUID{ 25 }) } } } } },
				} }) },
				{ "PrefabInstances", nlohmann::json::array({ ToJson(instance) }) },
			};
			const auto original = scene;
			std::string diagnostic;
			if (!PrefabReferenceRemapper::NormalizeLegacySceneInstances(scene, {}, diagnostic) || diagnostic.empty() ||
				scene["PrefabInstances"][0]["EntityMap"].size() != 3 ||
				scene["Entities"][0]["Components"]["Hierarchy"]["parentLocalFileID"] != ToString(UUID{ 22 }) ||
				scene["Entities"][0]["Components"]["ScriptComponent"]["sceneRef"]["localFileId"] != ToString(UUID{ 20 }) ||
				scene["Entities"][0]["Components"]["ScriptComponent"]["prefabRef"] != prefabRef ||
				scene["Entities"][0]["Components"]["ScriptComponent"]["foreignRef"] != foreignRef ||
				scene["Entities"][0]["Components"]["ScriptComponent"]["text"] != ToString(UUID{ 25 })) return false;
			PrefabInstanceData restored;
			if (!FromJson(scene["PrefabInstances"][0], restored)) return false;
			const auto normalized = scene;
			if (!PrefabReferenceRemapper::NormalizeLegacySceneInstances(scene, {}, diagnostic) ||
				!diagnostic.empty() || scene != normalized) return false;
			auto conflict = original;
			conflict["Entities"][0]["LocalFileID"] = ToString(UUID{ 21 });
			const auto unchanged = conflict;
			if (PrefabReferenceRemapper::NormalizeLegacySceneInstances(conflict, {}, diagnostic) ||
				conflict != unchanged) return false;
			auto leafScene = original;
			leafScene["PrefabInstances"][0]["Modifications"] = nlohmann::json::array({
				{ { "Target", ToString(UUID{ 10 }) }, { "Path", "ScriptComponent/ref/kind" }, { "Value", "Scene" } },
				{ { "Target", ToString(UUID{ 10 }) }, { "Path", "ScriptComponent/ref/sourceAsset" }, { "Value", "" } },
				{ { "Target", ToString(UUID{ 10 }) }, { "Path", "ScriptComponent/ref/localFileId" }, { "Value", ToString(UUID{ 21 }) } },
			});
			if (!PrefabReferenceRemapper::NormalizeLegacySceneInstances(leafScene, {}, diagnostic) ||
				leafScene["PrefabInstances"][0]["Modifications"][2]["Value"] != ToString(UUID{ 20 })) return false;
			auto nestedScene = original;
			auto nested = instance;
			nested.instanceID = UUID{ 2 };
			nested.ownerPrefabInstanceID = instance.instanceID;
			nested.nestedSlotID = UUID{ 3 };
			nested.entityMap = { { UUID{ 10 }, UUID{ 40 } }, { UUID{ 10 }, UUID{ 41 } } };
			nested.rootParentSceneLocalFileID = UUID{ 21 };
			nestedScene["PrefabInstances"][0]["NestedInstances"].push_back(ToJson(nested));
			if (!PrefabReferenceRemapper::NormalizeLegacySceneInstances(nestedScene, {}, diagnostic) ||
				nestedScene["PrefabInstances"][0]["NestedInstances"][0]["RootParent"] != ToString(UUID{ 20 }) ||
				!FromJson(nestedScene["PrefabInstances"][0], restored)) return false;
		}

		Engine::RuntimePaths::Refresh();
		const std::filesystem::path testRoot =
			Engine::RuntimePaths::GetGameAssetsRoot() / "Tests/PrefabImmediate";
		std::error_code ec;
		std::filesystem::remove_all(testRoot, ec);
		std::filesystem::create_directories(testRoot, ec);
		if (ec) {
			return false;
		}

		Engine::AssetDatabase database;
		database.Init();
		Engine::HierarchySystem hierarchySystem;
		Engine::PrefabSystem prefabSystem;
		Engine::ECSWorld sourceWorld;
		const Engine::Entity sourceRoot =
			Engine::SceneAuthoring::CreateGameObject(sourceWorld, "ImmediateRoot");
		const Engine::Entity sourceChild =
			Engine::SceneAuthoring::CreateGameObject(sourceWorld, "ImmediateChild");
		sourceWorld.AddComponent<Engine::CollisionComponent>(sourceRoot).enabled = false;
		hierarchySystem.SetParent(sourceWorld, sourceChild, sourceRoot);

		const std::string prefabPath =
			"game://Tests/PrefabImmediate/Immediate.prefab.json";
		bool passed = prefabSystem.SavePrefab(
			database, sourceWorld, sourceRoot, prefabPath);
		const Engine::AssetID prefabAsset = database.ImportOrGet(
			prefabPath, Engine::AssetType::Prefab);

		Engine::ECSWorld targetWorld;
		const Engine::Entity targetParent =
			Engine::SceneAuthoring::CreateGameObject(targetWorld, "Parent");
		Engine::PrefabInstantiateDesc desc{};
		desc.parent = targetParent;
		Engine::PrefabInstantiateResult result{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, targetWorld, prefabAsset, result, desc);

		const Engine::HierarchyComponent* rootHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(result.root);
		const Engine::Entity child = rootHierarchy ? rootHierarchy->firstChild : Engine::Entity::Null();
		const Engine::HierarchyComponent* childHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(child);
		const Engine::NameComponent* childName =
			targetWorld.TryGetComponent<Engine::NameComponent>(child);
		passed &= targetWorld.IsAlive(result.root) && result.createdEntities.size() == 2 &&
			targetWorld.HasComponent<Engine::CollisionComponent>(result.root) &&
			rootHierarchy && rootHierarchy->parent == targetParent &&
			targetWorld.IsAlive(child) && childHierarchy && childHierarchy->parent == result.root &&
			childName && childName->name == "ImmediateChild";

		// 生成予約中の親へ複数のPrefabを生成し、予約反映後も階層を維持する
		for (int parentState = 0; parentState < 3; ++parentState) {

			using namespace Engine;
			ECSWorld pendingWorld;
			const Entity pendingParent = pendingWorld.CreateEntity();
			if (parentState == 1) {
				SceneObjectUtility::EnsureSceneObject(pendingWorld, pendingParent);
			} else if (parentState == 2) {
				pendingWorld.AddComponent<HierarchyComponent>(pendingParent);
			}
			auto& commands = pendingWorld.GetCommandBuffer();
			commands.EnqueueCreateEntity(pendingParent, "RainVisuals", Entity::Null());
			passed &= commands.StageCreatePosition(pendingParent, Vector3{ 2.0f, 3.0f, 4.0f });

			PrefabInstantiateDesc pendingDesc{};
			pendingDesc.parent = pendingParent;
			PrefabInstantiateResult firstResult{};
			PrefabInstantiateResult secondResult{};
			passed &= prefabSystem.InstantiatePrefab(
				database, hierarchySystem, pendingWorld, prefabAsset, firstResult, pendingDesc);
			passed &= prefabSystem.InstantiatePrefab(
				database, hierarchySystem, pendingWorld, prefabAsset, secondResult, pendingDesc);
			const SceneObjectComponent* parentSceneObject =
				pendingWorld.TryGetComponent<SceneObjectComponent>(pendingParent);
			const Engine::UUID parentLocalFileID = parentSceneObject ? parentSceneObject->localFileID : Engine::UUID{};

			// 構造変更をまたいでコンポーネント参照を保持しない
			const auto checkHierarchy = [&]() {

				const auto* parentHierarchy = pendingWorld.TryGetComponent<HierarchyComponent>(pendingParent);
				const auto* firstHierarchy = pendingWorld.TryGetComponent<HierarchyComponent>(firstResult.root);
				const auto* secondHierarchy = pendingWorld.TryGetComponent<HierarchyComponent>(secondResult.root);
				const auto* sceneObject = pendingWorld.TryGetComponent<SceneObjectComponent>(pendingParent);
				return parentHierarchy && firstHierarchy && secondHierarchy && sceneObject && parentLocalFileID &&
					sceneObject->localFileID == parentLocalFileID &&
					parentHierarchy->firstChild == firstResult.root && parentHierarchy->lastChild == secondResult.root &&
					firstHierarchy->parent == pendingParent && secondHierarchy->parent == pendingParent &&
					firstHierarchy->parentLocalFileID == parentLocalFileID &&
					secondHierarchy->parentLocalFileID == parentLocalFileID &&
					firstHierarchy->prevSibling == Entity::Null() && firstHierarchy->nextSibling == secondResult.root &&
					secondHierarchy->prevSibling == firstResult.root && secondHierarchy->nextSibling == Entity::Null() &&
					firstResult.createdEntities.size() == 2 && secondResult.createdEntities.size() == 2 &&
					pendingWorld.IsAlive(firstHierarchy->firstChild) && pendingWorld.IsAlive(secondHierarchy->firstChild);
			};
			passed &= commands.IsPendingCreate(pendingParent) && checkHierarchy();
			commands.Flush(pendingWorld);
			passed &= !commands.IsPendingCreate(pendingParent) && checkHierarchy();
			const auto* parentName = pendingWorld.TryGetComponent<NameComponent>(pendingParent);
			const auto* parentTransform = pendingWorld.TryGetComponent<TransformComponent>(pendingParent);
			passed &= parentName && parentName->name == "RainVisuals" && parentTransform &&
				parentTransform->localPos.x == 2.0f && parentTransform->localPos.y == 3.0f &&
				parentTransform->localPos.z == 4.0f;
		}

		// 旧シーンをロードし、別名を参照する通常Entityの親も復旧する
		{
			using namespace Engine;
			const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(database, prefabAsset);
			using Engine::UUID;
			auto data = PrefabOverrideUtility::CaptureInstance(targetWorld, database, result.prefabInstanceID, base);
			data.rootParentSceneLocalFileID = {};
			const UUID localID = targetWorld.GetComponent<PrefabLinkComponent>(child).prefabLocalFileID;
			const UUID canonical = targetWorld.GetComponent<SceneObjectComponent>(child).localFileID;
			auto legacy = ToJson(data);
			legacy["EntityMap"].push_back({ { "P", ToString(localID) }, { "S", ToString(UUID{ 999 }) } });
			nlohmann::json scene = { { "PrefabInstances", nlohmann::json::array({ legacy }) },
				{ "Entities", nlohmann::json::array({ { { "LocalFileID", ToString(UUID{ 998 }) },
					{ "Components", { { "Hierarchy", { { "parentLocalFileID", ToString(UUID{ 999 }) } } } } } } }) } };
			SceneSystem scenes;
			ECSWorld restored;
			passed &= scenes.LoadFromJson(scene, restored, &database, {}, UUID{ 701 });
			const Entity observer = SceneObjectUtility::FindByLocalFileID(restored, UUID{ 998 });
			const Entity restoredChild = SceneObjectUtility::FindByLocalFileID(restored, canonical);
			passed &= restored.IsAlive(observer) && restored.IsAlive(restoredChild) &&
				restored.GetComponent<HierarchyComponent>(observer).parent == restoredChild;
			SceneHeader header;
			header.guid = AssetGUID::New();
			SceneSaveSnapshot snapshot;
			passed &= scenes.CaptureSaveSnapshot(testRoot / "Recovered.scene.json", restored, header, database, snapshot);
			std::string diagnostic;
			passed &= PrefabReferenceRemapper::NormalizeLegacySceneInstances(snapshot.root, header.guid, diagnostic) &&
				diagnostic.empty();
			ECSWorld reloaded;
			passed &= scenes.LoadFromJson(snapshot.root, reloaded, &database, header.guid, UUID{ 702 });
			snapshot.useExternalActors = false;
			passed &= SceneSystem::WriteSaveSnapshot(snapshot);
			const auto savedScene = JsonAdapter::Load(testRoot / "Recovered.scene.json", false);

			// 生存中の重複は旧データ扱いせず、保存と伝播を中止して実体を保持する
			const Entity duplicate = SceneAuthoring::CreateGameObject(restored, "Duplicate");
			restored.AddComponent<PrefabLinkComponent>(duplicate) = restored.GetComponent<PrefabLinkComponent>(restoredChild);
			hierarchySystem.SetParent(restored, duplicate, restoredChild);
			passed &= !scenes.CaptureSaveSnapshot(testRoot / "Recovered.scene.json", restored, header, database, snapshot);
			passed &= !scenes.SaveScene(testRoot / "Recovered.scene.json", restored, header, database) &&
				JsonAdapter::Load(testRoot / "Recovered.scene.json", false) == savedScene;
			passed &= !PrefabOverrideUtility::PropagateToInstances(restored, database, hierarchySystem, prefabAsset, base) &&
				restored.IsAlive(duplicate) && restored.IsAlive(restoredChild);
		}

		const Engine::Entity runtimeParent =
			Engine::SceneAuthoring::CreateGameObject(targetWorld, "RuntimeParent");
		targetWorld.GetCommandBuffer().EnqueueSetParent(child, runtimeParent, false);
		targetWorld.GetCommandBuffer().Flush(targetWorld);
		childHierarchy = targetWorld.TryGetComponent<Engine::HierarchyComponent>(child);
		passed &= childHierarchy && childHierarchy->parent == runtimeParent &&
			targetWorld.HasComponent<Engine::PrefabLinkComponent>(child);

		Engine::PrefabInstantiateResult destroyResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, targetWorld, prefabAsset, destroyResult, desc);
		const Engine::Entity destroyedRoot = destroyResult.root;
		targetWorld.GetCommandBuffer().EnqueueDestroyEntity(destroyedRoot);
		targetWorld.GetCommandBuffer().Flush(targetWorld);
		targetWorld.FlushPendingDestroyEntities();
		const Engine::HierarchyComponent* targetParentHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(targetParent);
		passed &= !targetWorld.IsAlive(destroyedRoot) && targetParentHierarchy &&
			(!targetParentHierarchy->firstChild.IsValid() ||
				targetWorld.IsAlive(targetParentHierarchy->firstChild)) &&
			(!targetParentHierarchy->lastChild.IsValid() ||
				targetWorld.IsAlive(targetParentHierarchy->lastChild));

		Engine::PrefabInstantiateResult childDestroyResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, targetWorld, prefabAsset, childDestroyResult, desc);
		const Engine::HierarchyComponent* childDestroyRootHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(childDestroyResult.root);
		const Engine::Entity destroyedChild = childDestroyRootHierarchy ?
			childDestroyRootHierarchy->firstChild : Engine::Entity::Null();
		targetWorld.GetCommandBuffer().EnqueueDestroyEntity(destroyedChild);
		targetWorld.GetCommandBuffer().Flush(targetWorld);
		targetWorld.FlushPendingDestroyEntities();
		childDestroyRootHierarchy =
			targetWorld.TryGetComponent<Engine::HierarchyComponent>(childDestroyResult.root);
		passed &= !targetWorld.IsAlive(destroyedChild) && childDestroyRootHierarchy &&
			!childDestroyRootHierarchy->firstChild.IsValid() &&
			!childDestroyRootHierarchy->lastChild.IsValid();

		const std::string emptyPrefabPath =
			"game://Tests/PrefabImmediate/Empty.prefab.json";
		nlohmann::json emptyPrefab = nlohmann::json::object();
		emptyPrefab["Entities"] = nlohmann::json::array();
		emptyPrefab["Header"] = {
			{ "name", "EmptyPrefab" },
			{ "rootLocalFileID", "" },
			{ "version", 2 }
		};
		emptyPrefab["NestedPrefabInstances"] = nlohmann::json::array();
		emptyPrefab["SchemaVersion"] = 2;
		passed &= Engine::JsonAdapter::SaveCanonical(
			database.ResolveAssetPath(emptyPrefabPath), emptyPrefab);
		const Engine::AssetID emptyPrefabAsset = database.ImportOrGet(
			emptyPrefabPath, Engine::AssetType::Prefab);
		Engine::PrefabInstantiateResult emptyResult{};
		passed &= prefabSystem.InstantiatePrefab(
			database, hierarchySystem, targetWorld, emptyPrefabAsset, emptyResult);
		passed &= targetWorld.IsAlive(emptyResult.root) &&
			emptyResult.createdEntities.size() == 1 &&
			targetWorld.HasComponent<Engine::PrefabLinkComponent>(emptyResult.root) &&
			targetWorld.GetComponent<Engine::NameComponent>(emptyResult.root).name == "EmptyPrefab";

		std::filesystem::remove_all(testRoot, ec);
		return passed && !ec;
	}
}
