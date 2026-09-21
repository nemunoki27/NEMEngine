#include "SceneSnapshotBuilder.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Utility/Algorithm/Algorithm.h>
#include <Engine/Core/World/Scene/Runtime/SceneSystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneDocument.h>

// c++
#include <algorithm>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <vector>

using namespace Engine::SceneDocument;

namespace {

	uint64_t LocalFileIDOf(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::SceneObjectComponent>(entity)) {
			return 0;
		}
		return world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID.value;
	}

	void SortEntitiesByLocalFileID(Engine::ECSWorld& world, std::vector<Engine::Entity>& entities) {

		for (const Engine::Entity& entity : entities) {
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
		}
		std::sort(entities.begin(), entities.end(), [&world](const Engine::Entity& lhs,
			const Engine::Entity& rhs) {

			const uint64_t lhsID = LocalFileIDOf(world, lhs);
			const uint64_t rhsID = LocalFileIDOf(world, rhs);
			if (lhsID != rhsID) {
				return lhsID < rhsID;
			}
			if (lhs.index != rhs.index) {
				return lhs.index < rhs.index;
			}
			return lhs.generation < rhs.generation;
			});
	}

	bool ValidateWorldLocalFileIDs(Engine::ECSWorld& world,
		const std::vector<Engine::Entity>& entities) {

		std::unordered_set<Engine::UUID> ids;
		for (const Engine::Entity& entity : entities) {

			if (!world.IsAlive(entity)) {
				continue;
			}
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);
			const Engine::UUID localFileID =
				world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID;
			if (!ids.insert(localFileID).second) {
				Engine::Logger::Output(Engine::LogType::Engine, spdlog::level::err,
					"[SceneSystem] LocalFileIDが重複しています ID={}", Engine::ToString(localFileID));
				return false;
			}
		}
		return true;
	}
}

bool Engine::SceneSnapshotBuilder::CaptureSaveSnapshot(
	const std::filesystem::path& scenePath, ECSWorld& world,
	const SceneHeader& header, AssetDatabase& database,
	SceneSaveSnapshot& outSnapshot,
	const std::vector<Entity>* entitiesSubset) {

	std::vector<Entity> sceneEntities;
	if (entitiesSubset) {
		sceneEntities = *entitiesSubset;
	} else {
		world.ForEachAliveEntity([&sceneEntities](Entity entity) {
			sceneEntities.emplace_back(entity);
			});
	}
	if (!ValidateWorldLocalFileIDs(world, sceneEntities)) {
		return false;
	}

	nlohmann::json root = nlohmann::json::object();
	root["SchemaVersion"] = kSceneSchemaVersion;
	root["Header"] = ToJson(header);

	// 対象実体の中からプレファブインスタンスを集め、薄い差分形式で保存する
	// インスタンスに取り込まれた実体のシーンローカルIDを覚えておき、fat側の重複保存を防ぐ
	std::unordered_set<UUID> consumedSceneLocalIDs;
	std::unordered_map<UUID, AssetID> instanceToPrefab;
	bool invalidPrefabLink = false;
	PrefabOverrideUtility::SynchronizeNestedPrefabOwnership(world);

	auto collectInstanceIDs = [&](const Entity& entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		if (!link.prefabAsset || !link.prefabInstanceID || !link.prefabLocalFileID) {
			invalidPrefabLink = true;
			return;
		}
		if (link.ownerPrefabInstanceID) {
			return;
		}
		auto [it, inserted] = instanceToPrefab.emplace(link.prefabInstanceID, link.prefabAsset);
		if (!inserted && it->second != link.prefabAsset) {
			invalidPrefabLink = true;
		}
		};
	if (entitiesSubset) {
		for (const Entity& entity : *entitiesSubset) {
			if (world.IsAlive(entity)) {
				collectInstanceIDs(entity);
			}
		}
	} else {
		world.ForEachAliveEntity(collectInstanceIDs);
	}
	if (invalidPrefabLink) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] 不正なPrefabLinkがあるためシーンを保存できません");
		return false;
	}

	// プレファブインスタンスごとに差分を抽出する
	nlohmann::json prefabInstances = nlohmann::json::array();
	std::vector<std::pair<UUID, AssetID>> sortedInstances(
		instanceToPrefab.begin(), instanceToPrefab.end());
	std::sort(sortedInstances.begin(), sortedInstances.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.first.value < rhs.first.value;
		});
	for (const auto& [instanceID, prefabAsset] : sortedInstances) {

		const std::vector<Entity> instanceEntities =
			PrefabOverrideUtility::CollectInstanceEntities(world, instanceID);
		const size_t rootCount = static_cast<size_t>(std::count_if(
			instanceEntities.begin(), instanceEntities.end(), [&](const Entity& entity) {
				return world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity) &&
					world.GetComponent<PrefabLinkComponent>(entity).isPrefabRoot;
				}));
		if (rootCount != 1) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SceneSystem] Prefabインスタンスのルート数が不正です InstanceID={} RootCount={}",
				ToString(instanceID), rootCount);
			return false;
		}

		const auto base = PrefabOverrideUtility::LoadPrefabBaseEntities(database, prefabAsset);
		if (base.empty()) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SceneSystem] Prefabアセットを読み込めないためシーンを保存できません AssetID={}",
				ToString(prefabAsset));
			return false;
		}
		PrefabInstanceData data = PrefabOverrideUtility::CaptureInstance(world, database, instanceID, base);
		data.prefabAsset = prefabAsset;
		PrefabInstanceData validated;
		if (!FromJson(ToJson(data), validated)) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SceneSystem] Prefab対応が不正なため保存を中止します AssetID={} InstanceID={}",
				ToString(prefabAsset), ToString(instanceID));
			return false;
		}
		if (data.entityMap.empty()) {

			Logger::Output(LogType::Engine, spdlog::level::err,
				"[SceneSystem] PrefabインスタンスのEntity対応が空です InstanceID={}",
				ToString(instanceID));
			return false;
		}

		// 親Prefabが所有するネストPrefabを含めてfat側から除外する
		std::vector<const PrefabInstanceData*> pendingInstances = { &data };
		while (!pendingInstances.empty()) {

			const PrefabInstanceData* current = pendingInstances.back();
			pendingInstances.pop_back();
			for (const auto& [prefabLocal, sceneLocal] : current->entityMap) {
				consumedSceneLocalIDs.insert(sceneLocal);
			}
			for (const auto& added : current->addedEntities) {
				consumedSceneLocalIDs.insert(added.sceneLocalFileID);
			}
			for (const auto& nested : current->nestedInstances) {
				pendingInstances.emplace_back(&nested);
			}
		}
		prefabInstances.push_back(ToJson(data));
	}
	root["PrefabInstances"] = std::move(prefabInstances);

	// インスタンスに取り込まれなかった実体だけをfat保存する
	std::vector<Entity> fatEntities;
	auto appendFatEntity = [&](const Entity& entity) {

		if (!world.IsAlive(entity)) {
			return;
		}
		if (world.HasComponent<SceneObjectComponent>(entity) &&
			consumedSceneLocalIDs.count(world.GetComponent<SceneObjectComponent>(entity).localFileID)) {
			return;
		}
		fatEntities.emplace_back(entity);
		};
	if (entitiesSubset) {
		for (const Entity& entity : *entitiesSubset) {
			appendFatEntity(entity);
		}
	} else {
		world.ForEachAliveEntity(appendFatEntity);
	}
	root["Entities"] = SerializeEntities(world, &fatEntities);
	std::string diagnostic;
	if (!PrefabReferenceRemapper::NormalizeLegacySceneInstances(root, header.guid, diagnostic, &database) ||
		!diagnostic.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[SceneSystem] シーン内のID競合により保存を中止します path={} 詳細={}",
			Algorithm::PathToUTF8(scenePath), diagnostic);
		return false;
	}

	outSnapshot.scenePath = scenePath;
	outSnapshot.sceneAsset = header.guid;
	outSnapshot.root = std::move(root);
	outSnapshot.useExternalActors =
		ShouldUseExternalActors(scenePath);
	return true;
}

nlohmann::json Engine::SceneSnapshotBuilder::SerializeEntities(ECSWorld& world, const std::vector<Entity>* subset) {

	nlohmann::json array = nlohmann::json::array();
	std::vector<Entity> entities;

	if (subset) {
		for (const Entity& entity : *subset) {
			if (world.IsAlive(entity)) {
				entities.emplace_back(entity);
			}
		}
	} else {
		world.ForEachAliveEntity([&](const Entity& entity) {
			entities.emplace_back(entity);
			});
	}
	SortEntitiesByLocalFileID(world, entities);

	for (const Entity& entity : entities) {
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		nlohmann::json entityJson = nlohmann::json::object();
		entityJson["LocalFileID"] = ToString(sceneObject.localFileID);

		nlohmann::json components = nlohmann::json::object();
		world.SerializeEntityComponents(entity, components);
		entityJson["Components"] = std::move(components);
		array.push_back(std::move(entityJson));
	}
	return array;
}
