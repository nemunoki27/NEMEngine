#include "PrefabNestedIdentityTests.h"
#include "TestFixtures.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabSystem.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Components/Camera/CameraControllerComponent.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Scene/Serialization/EntitySnapshotBatchDuplicator.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <algorithm>
#include <iostream>

bool NEMTests::TestPrefabNestedAddedIdentity() {

	using namespace Engine;
	TestDirectory directory("NestedAddedIdentity", RuntimePaths::GetGameAssetsRoot());
	AssetDatabase database;
	database.Init();
	HierarchySystem hierarchy;
	PrefabSystem prefabs;
	const auto innerPath = directory.GetPath() / "Inner.prefab.json";
	const auto outerPath = directory.GetPath() / "Outer.prefab.json";
	const auto entity = [](uint64_t localID, const char* name) {
		return nlohmann::json{ { "LocalFileID", ToString(Engine::UUID{ localID }) },
			{ "Components", { { "Name", { { "name", name } } } } } };
	};
	const auto file = [&](uint64_t rootID, const char* name) {
		return nlohmann::json{ { "SchemaVersion", 2 },
			{ "Header", { { "rootLocalFileID", ToString(Engine::UUID{ rootID }) } } },
			{ "Entities", nlohmann::json::array({ entity(rootID, name) }) } };
	};
	if (!JsonFile::Save(innerPath, file(1, "Inner"))) {
		return false;
	}
	const AssetID innerAsset = database.ImportOrGet(RuntimePaths::ToAssetPath(innerPath), AssetType::Prefab);
	if (!innerAsset) {
		return false;
	}
	PrefabInstanceData nested;
	nested.prefabAsset = innerAsset;
	nested.instanceID = Engine::UUID{ 500 };
	nested.nestedSlotID = Engine::UUID{ 600 };
	nested.entityMap = { { Engine::UUID{ 1 }, Engine::UUID{ 100 } } };
	nested.rootParentSceneLocalFileID = Engine::UUID{ 10 };
	for (uint64_t index = 0; index < 2; ++index) {
		PrefabAddedEntity added;
		added.sceneLocalFileID = Engine::UUID{ 200 + index };
		added.parentSceneLocalFileID = Engine::UUID{ 100 };
		added.components = { { "Name", { { "name", index == 0 ? "Alpha" : "Beta" } } } };
		nested.addedEntities.emplace_back(std::move(added));
	}
	auto outer = file(10, "Outer");
	outer["Entities"][0]["Components"]["CameraController"]["follow"]["target"] = ToString(Engine::UUID{ 200 });
	outer["NestedPrefabInstances"] = nlohmann::json::array({ ToJson(nested) });
	if (!JsonFile::Save(outerPath, outer)) {
		return false;
	}
	const AssetID outerAsset = database.ImportOrGet(RuntimePaths::ToAssetPath(outerPath), AssetType::Prefab);
	ECSWorld world;
	PrefabInstantiateResult generated;
	PrefabInstantiateDesc desc;
	desc.ownerSceneInstanceID = Engine::UUID{ 800 };
	if (!outerAsset || !prefabs.InstantiatePrefab(database, hierarchy, world, outerAsset, generated, desc)) {
		return false;
	}
	const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(database, outerAsset);
	const auto captured = PrefabOverrideUtility::CaptureInstance(world, database, generated.prefabInstanceID, base);
	auto serialized = ToJson(captured);
	if (!serialized.contains("NestedInstances") || serialized["NestedInstances"].size() != 1) {
		return false;
	}
	// 保存配列を宣言順と逆にして位置依存の対応を検出する
	auto& added = serialized["NestedInstances"][0]["AddedEntities"];
	if (added.size() != 2) {
		return false;
	}
	std::sort(added.begin(), added.end(), [](const auto& lhs, const auto& rhs) {
		return lhs["Components"]["Name"]["name"].template get<std::string>() >
			rhs["Components"]["Name"]["name"].template get<std::string>();
	});
	PrefabInstanceData restored;
	if (!FromJson(serialized, restored)) {
		return false;
	}
	// 未編集の参照はPrefab宣言から復元させる
	restored.modifications.clear();
	ECSWorld reloaded;
	const Entity root = PrefabOverrideUtility::RebuildInstance(reloaded, database, hierarchy, restored, desc.ownerSceneInstanceID);
	if (!reloaded.IsAlive(root)) {
		return false;
	}
	const auto& follow = reloaded.GetComponent<CameraControllerComponent>(root).follow;
	const Entity target = SceneObjectUtility::FindByLocalFileID(reloaded, desc.ownerSceneInstanceID, follow.target);
	const std::string name = reloaded.IsAlive(target) ? reloaded.GetComponent<NameComponent>(target).name : "unresolved";
	if (name != "Alpha") {
		std::cerr << "Nested added reference expected=Alpha actual=" << name << '\n';
	}
	if (name != "Alpha" ||
		SceneObjectUtility::ResolveReference(reloaded, innerAsset, follow.target, desc.ownerSceneInstanceID) != target) {
		return false;
	}

	// 階層複製後も宣言IDと追加Entityの対応を保つ
	EntityTreeSnapshot snapshot;
	EntitySnapshotUtility::CaptureSubtree(reloaded, root, snapshot);
	const auto copies = EntitySnapshotBatchDuplicator::Build(std::vector<EntityTreeSnapshot>{ snapshot });
	ECSWorld copied;
	const auto entities = EntitySnapshotUtility::RestoreSubtree(copied, copies.front());
	hierarchy.RebuildRuntimeLinks(copied, entities);
	const Entity copiedRoot = copied.FindByUUID(copies.front().rootStableUUID);
	if (!copied.IsAlive(copiedRoot)) {
		return false;
	}
	const auto& copiedLink = copied.GetComponent<PrefabLinkComponent>(copiedRoot);
	const auto copiedData = PrefabOverrideUtility::CaptureInstance(copied, database, copiedLink.prefabInstanceID, base);
	if (copiedData.nestedInstances.size() != 1 || copiedData.nestedInstances.front().addedEntityMap.size() != 2) {
		return false;
	}
	const auto& copiedNested = copiedData.nestedInstances.front();
	for (const auto& [declarationID, localID] : copiedNested.addedEntityMap) {
		const Entity addedEntity = SceneObjectUtility::FindByLocalFileID(copied, desc.ownerSceneInstanceID, localID);
		const char* expected = declarationID == Engine::UUID{ 200 } ? "Alpha" : "Beta";
		if (!copied.IsAlive(addedEntity) || copied.GetComponent<NameComponent>(addedEntity).name != expected ||
			std::any_of(restored.nestedInstances.front().addedEntityMap.begin(),
				restored.nestedInstances.front().addedEntityMap.end(), [&](const auto& entry) { return entry.second == localID; })) {
			return false;
		}
	}
	const auto& copiedFollow = copied.GetComponent<CameraControllerComponent>(copiedRoot).follow;
	const Entity copiedTarget = SceneObjectUtility::FindByLocalFileID(copied, desc.ownerSceneInstanceID, copiedFollow.target);
	return copied.IsAlive(copiedTarget) && copied.GetComponent<NameComponent>(copiedTarget).name == "Alpha";
}
