#include "TestContracts.h"
#include "TestFixtures.h"
#include "EntitySnapshotTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSCreationScope.h>
#include <Engine/Core/World/Scene/Serialization/SceneInstantiator.h>
#include <Engine/Core/World/Scene/Serialization/EntityTreeSnapshot.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiator.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabInstanceRebuilder.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonFile.h>
#include <Engine/Core/Runtime/Paths/RuntimePaths.h>

// c++
#include <stdexcept>

namespace {

	size_t CountAlive(Engine::ECSWorld& world) {

		size_t count = 0;
		world.ForEachAliveEntity([&](Engine::Entity) { ++count; });
		return count;
	}

	void FailDestroy([[maybe_unused]] Engine::ECSWorld& world, [[maybe_unused]] const Engine::Entity& entity,
		[[maybe_unused]] uint32_t typeID, Engine::ComponentMutationKind kind, [[maybe_unused]] void* data) {

		if (kind == Engine::ComponentMutationKind::EntityDestroyed) {
			throw std::runtime_error("取消通知の失敗を検証");
		}
	}
}

bool NEMTests::TestCreationScopes() {

	using namespace Engine;
	if (!TestEntitySnapshotDuplication()) {
		return false;
	}
	ECSWorld world;
	const Entity existing = SceneAuthoring::CreateGameObject(world, "Existing");
	const Entity pending = world.CreateEntity();
	world.DestroyEntity(pending);
	Entity first;
	Entity nested;
	{
		ECSCreationScope outer(world);
		first = world.CreateEntity();
		{
			ECSCreationScope inner(world);
			nested = world.CreateEntity();
			inner.Commit();
		}
	}
	// 入れ子の成功も外側で取り消し、既存の削除予約は進めない
	bool passed = !world.IsAlive(first) && !world.IsAlive(nested) &&
		world.IsAlive(existing) && world.IsAlive(pending);
	{
		ECSCreationScope scope(world);
		first = world.CreateEntity();
		scope.Commit();
	}
	passed &= world.IsAlive(first);
	world.DestroyEntity(first);
	world.FlushPendingDestroyEntities();
	passed &= !world.IsAlive(pending) && !world.IsAlive(first);

	// 破棄通知が投げても全生成物を回収する
	const uint64_t listener = world.AddComponentMutationListener(&FailDestroy, nullptr);
	{
		ECSCreationScope scope(world);
		first = world.CreateEntity();
		nested = world.CreateEntity();
		bool failed = false;
		try {
			scope.Rollback();
		} catch (const std::runtime_error&) {
			failed = true;
		}
		passed &= failed && !world.IsAlive(first) && !world.IsAlive(nested);
	}
	world.RemoveComponentMutationListener(listener);
	HierarchySystem hierarchy;
	const Entity existingChild = SceneAuthoring::CreateGameObject(world, "ExistingChild");
	hierarchy.SetParent(world, existingChild, existing);
	{
		SceneCreationScope scope(world);
		bool rejected = false;
		try {
			scope.DestroyCreated(existingChild);
		} catch (const std::invalid_argument&) {
			rejected = true;
		}
		// 範囲外を誤指定しても親子関係へ触れない
		passed &= rejected && world.IsAlive(existingChild) &&
			world.GetComponent<HierarchyComponent>(existingChild).parent == existing;
	}

	TestDirectory directory("CreationScopes", RuntimePaths::GetGameAssetsRoot());
	const auto prefabPath = directory.GetPath() / "Broken.prefab.json";
	const nlohmann::json root = {
		{ "LocalFileID", "0000000000000001" },
		{ "Components", { { "Name", { { "name", "Root" } } } } }
	};
	nlohmann::json broken = root;
	broken["LocalFileID"] = "0000000000000002";
	broken["Components"]["Name"]["name"] = 123;
	const nlohmann::json brokenScene = { { "Entities", nlohmann::json::array({ root, broken }) } };
	const Entity unrelated = world.CreateEntity();
	world.DestroyEntity(unrelated);
	const size_t previousCount = CountAlive(world);

	// 階層Snapshotの後半で失敗しても先に復元したEntityを残さない
	EntityTreeSnapshot snapshot;
	EntitySnapshotUtility::CaptureSubtree(world, existing, snapshot);
	for (auto& entry : snapshot.entities) {
		entry.stableUUID = Engine::UUID::New();
	}
	snapshot.rootStableUUID = snapshot.entities.front().stableUUID;
	{
		// 入れ子Prefabの参照元をルートのSceneで上書きしない
		SceneCreationScope scope(world);
		snapshot.ownerSceneInstanceID = Engine::UUID{ 55 };
		snapshot.ownerSourceAsset = AssetID{ 1, 2 };
		snapshot.entities.back().sourceAsset = AssetID{ 3, 4 };
		const auto restored = EntitySnapshotUtility::RestoreSubtree(world, snapshot);
		const auto& rootMembership = world.GetComponent<SceneObjectComponent>(restored.front());
		const auto& childMembership = world.GetComponent<SceneObjectComponent>(restored.back());
		passed &= rootMembership.sceneInstanceID == snapshot.ownerSceneInstanceID &&
			rootMembership.sourceAsset == snapshot.ownerSourceAsset &&
			childMembership.sceneInstanceID == snapshot.ownerSceneInstanceID &&
			childMembership.sourceAsset == snapshot.entities.back().sourceAsset;
	}
	snapshot.entities.back().components["Name"]["name"] = 123;
	bool restoreFailed = false;
	try {
		EntitySnapshotUtility::RestoreSubtree(world, snapshot);
	} catch (const std::exception&) {
		restoreFailed = true;
	}
	passed &= restoreFailed && CountAlive(world) == previousCount && world.IsAlive(unrelated);

	std::vector<Entity> output{ existing };
	passed &= !SceneInstantiator::LoadFromJson(brokenScene, world, nullptr, {}, {}, &output) &&
		CountAlive(world) == previousCount && world.IsAlive(unrelated) && output == std::vector<Entity>{ existing };

	// 不正な保存配列やComponentを空のデータとして読み込まない
	for (const auto& invalid : { nlohmann::json{}, nlohmann::json::object(), nlohmann::json(123) }) {
		for (const char* key : { "Entities", "PrefabInstances" }) {
			auto malformed = nlohmann::json{ { "Entities", nlohmann::json::array({ root }) } };
			malformed[key] = invalid;
			passed &= !SceneInstantiator::LoadFromJson(malformed, world, nullptr, {}, {}, &output) &&
				CountAlive(world) == previousCount && output == std::vector<Entity>{ existing };
		}
	}
	for (const auto& invalid : { nlohmann::json{}, nlohmann::json::array(), nlohmann::json(123) }) {
		auto malformedEntity = broken;
		malformedEntity["Components"] = invalid;
		const nlohmann::json malformed = { { "Entities", nlohmann::json::array({ root, malformedEntity }) } };
		passed &= !SceneInstantiator::LoadFromJson(malformed, world, nullptr, {}, {}, &output) &&
			CountAlive(world) == previousCount && world.IsAlive(unrelated) && output == std::vector<Entity>{ existing };
	}

	nlohmann::json prefab = {
		{ "SchemaVersion", 2 }, { "Header", { { "rootLocalFileID", "0000000000000001" } } },
		{ "Entities", nlohmann::json::array({ root, broken }) }
	};
	if (!JsonFile::Save(prefabPath, prefab)) {
		return false;
	}
	AssetDatabase database;
	database.Init();
	const AssetID asset = database.ImportOrGet(RuntimePaths::ToAssetPath(prefabPath), AssetType::Prefab);
	PrefabInstantiateResult result;
	PrefabInstantiateDesc desc;
	desc.parent = existing;
	passed &= asset && !PrefabInstantiator::InstantiatePrefab(database, hierarchy, world, asset, result, desc) &&
		!result.root.IsValid() && result.createdEntities.empty() && CountAlive(world) == previousCount;

	// 外部の親へ接続した後のネスト失敗でも兄弟リンクを戻す
	prefab["Entities"] = nlohmann::json::array({ root });
	for (const auto& invalid : { nlohmann::json::object(), nlohmann::json::array({ nullptr }),
		nlohmann::json::array({ { { "PrefabAsset", 123 } } }) }) {
		prefab["NestedPrefabInstances"] = invalid;
		if (!JsonFile::Save(prefabPath, prefab)) {
			return false;
		}
		passed &= !PrefabInstantiator::InstantiatePrefab(database, hierarchy, world, asset, result, desc) &&
			!result.root.IsValid() && CountAlive(world) == previousCount && world.IsAlive(unrelated);
	}
	PrefabInstanceData missing;
	missing.prefabAsset = AssetID{ 99, 100 };
	missing.instanceID = Engine::UUID::New();
	missing.nestedSlotID = Engine::UUID::New();
	missing.entityMap = { { Engine::UUID{ 1 }, Engine::UUID{ 2 } } };
	PrefabInstanceData validatedMissing;
	passed &= FromJson(ToJson(missing), validatedMissing);
	prefab["NestedPrefabInstances"] = nlohmann::json::array({ ToJson(missing) });
	if (!JsonFile::Save(prefabPath, prefab)) {
		return false;
	}
	passed &= !PrefabInstantiator::InstantiatePrefab(database, hierarchy, world, asset, result, desc) &&
		CountAlive(world) == previousCount && world.IsAlive(unrelated);
	passed &= world.GetComponent<HierarchyComponent>(existing).firstChild == existingChild &&
		world.GetComponent<HierarchyComponent>(existing).lastChild == existingChild &&
		!world.GetComponent<HierarchyComponent>(existingChild).nextSibling.IsValid();

	// ベース生成後の差分読込失敗も取り消す
	prefab["Entities"] = nlohmann::json::array({ root });
	prefab.erase("NestedPrefabInstances");
	if (!JsonFile::Save(prefabPath, prefab)) {
		return false;
	}
	PrefabInstanceData data;
	data.prefabAsset = asset;
	data.instanceID = Engine::UUID::New();
	data.entityMap = { { Engine::UUID{ 1 }, Engine::UUID{ 10 } } };
	data.modifications.push_back({ Engine::UUID{ 1 }, "Name/name", 123 });
	passed &= !PrefabInstanceRebuilder::RebuildInstance(world, database, hierarchy, data, {}).IsValid() &&
		CountAlive(world) == previousCount && world.IsAlive(unrelated);
	passed &= world.GetComponent<HierarchyComponent>(existing).firstChild == existingChild;
	world.FlushPendingDestroyEntities();
	return passed && CountAlive(world) == 2;
}
