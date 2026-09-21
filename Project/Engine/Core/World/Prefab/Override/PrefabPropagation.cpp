#include "PrefabPropagation.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/Foundation/Diagnostics/Log.h>
#include <Engine/Core/Foundation/Serialization/Json/JsonSerializer.h>

// c++
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace {

	// 破棄→再生成の安全策、インスタンス実体の完全な状態を控えて再生成失敗時に元へ戻せるようにする
	struct InstanceEntityBackup {

		Engine::UUID stableUUID{};
		Engine::UUID localFileID{};
		Engine::UUID parentLocalFileID{};
		int32_t siblingOrder = 0;
		nlohmann::json components;
	};

	uint64_t EntityKey(const Engine::Entity& entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}

	// 別Prefabを境界としてインスタンスが所有する通常階層だけを収集する
	void CollectOwnedHierarchy(Engine::ECSWorld& world, const Engine::Entity& entity,
		std::unordered_set<Engine::UUID>& ownedInstances, std::vector<Engine::Entity>& out,
		std::unordered_set<uint64_t>& collected) {

		if (!world.IsAlive(entity) || !collected.insert(EntityKey(entity)).second) {
			return;
		}
		if (world.HasComponent<Engine::PrefabLinkComponent>(entity)) {

			const auto& link = world.GetComponent<Engine::PrefabLinkComponent>(entity);
			if (!ownedInstances.contains(link.prefabInstanceID)) {

				if (!ownedInstances.contains(link.ownerPrefabInstanceID)) {
					return;
				}
				ownedInstances.insert(link.prefabInstanceID);
			}
		}
		out.emplace_back(entity);
		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return;
		}

		Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(entity).firstChild;
		while (world.IsAlive(child)) {

			const Engine::Entity next = world.HasComponent<Engine::HierarchyComponent>(child) ?
				world.GetComponent<Engine::HierarchyComponent>(child).nextSibling : Engine::Entity::Null();
			CollectOwnedHierarchy(world, child, ownedInstances, out, collected);
			child = next;
		}
	}

	// ワールド全体の保存階層からランタイムリンクを再構築する
	void RebuildAllHierarchy(Engine::ECSWorld& world, Engine::HierarchySystem& hierarchySystem) {

		std::vector<Engine::Entity> scope;
		scope.reserve(world.GetRecordCount());
		world.ForEachAliveEntity([&](Engine::Entity entity) { scope.emplace_back(entity); });
		hierarchySystem.RebuildRuntimeLinks(world, scope);
	}

	// 破棄前に各実体のローカルID/親/兄弟順と全コンポーネントを控える
	std::vector<InstanceEntityBackup> CaptureInstanceBackup(Engine::ECSWorld& world, const std::vector<Engine::Entity>& entities) {

		std::vector<InstanceEntityBackup> backup;
		backup.reserve(entities.size());
		for (const Engine::Entity& entity : entities) {

			if (!world.IsAlive(entity)) {
				continue;
			}
			InstanceEntityBackup state{};
			state.stableUUID = world.GetUUID(entity);
			if (world.HasComponent<Engine::SceneObjectComponent>(entity)) {
				state.localFileID = world.GetComponent<Engine::SceneObjectComponent>(entity).localFileID;
			}
			if (world.HasComponent<Engine::HierarchyComponent>(entity)) {
				state.parentLocalFileID = world.GetComponent<Engine::HierarchyComponent>(entity).parentLocalFileID;
				state.siblingOrder = world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder;
			}
			world.SerializeEntityComponents(entity, state.components);
			backup.emplace_back(std::move(state));
		}
		return backup;
	}

	// 控えた状態から実体を作り直し、ローカルIDと親子付けを復元する
	void RestoreInstanceBackup(Engine::ECSWorld& world,
		const std::vector<InstanceEntityBackup>& backup, Engine::UUID sceneInstanceID) {

		std::vector<Engine::Entity> restored;
		restored.reserve(backup.size());
		for (const InstanceEntityBackup& state : backup) {

			const Engine::Entity entity = world.CreateEntity(state.stableUUID);
			if (state.components.is_object()) {
				for (auto it = state.components.begin(); it != state.components.end(); ++it) {
					world.AddComponentFromJson(entity, it.key(), it.value());
				}
			}
			Engine::SceneAuthoring::EnsureGameObjectDefaults(world, entity);

			// ローカルIDと所属シーン、親子付けは控えた値で確定させる
			auto& sceneObject = world.GetComponent<Engine::SceneObjectComponent>(entity);
			if (state.localFileID) {
				sceneObject.localFileID = state.localFileID;
			}
			sceneObject.sceneInstanceID = sceneInstanceID;
			auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
			hierarchy.parentLocalFileID = state.parentLocalFileID;
			hierarchy.siblingOrder = state.siblingOrder;
			restored.emplace_back(entity);
		}
	}

	// オーバーライド判定用にベースを正規化する
	// インスタンスは from_json -> 生成時後処理 -> to_json を経るため、生のプレファブJSONと差分が出て誤検出になる
	// ベースも同じ経路で一度通し、インスタンスと同じ表現へ揃えてから比較する
	std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> NormalizeBaseForDiff(
		Engine::AssetDatabase& database, const std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity>& base) {

		std::unordered_map<Engine::UUID, Engine::PrefabBaseEntity> normalized = base;
		Engine::ECSWorld temp;
		for (auto& [localID, baseEntity] : normalized) {

			const nlohmann::json sourceComponents = baseEntity.components;
			const Engine::Entity entity = temp.CreateEntity();
			if (sourceComponents.is_object()) {
				for (auto it = sourceComponents.begin(); it != sourceComponents.end(); ++it) {
					temp.AddComponentFromJson(entity, it.key(), it.value());
				}
			}
			// 生成時と同じサブメッシュ正規化を通し、インスタンス側の表現に揃える
			if (temp.HasComponent<Engine::MeshRendererComponent>(entity)) {
				Engine::MeshSubMeshAuthoring::SyncEntity(
					&database, temp, entity, true);
			}
			nlohmann::json normalizedComponents;
			temp.SerializeEntityComponents(entity, normalizedComponents);
			baseEntity.components = std::move(normalizedComponents);
		}
		return normalized;
	}
}

bool Engine::PrefabPropagation::PropagateToInstances(ECSWorld& world, AssetDatabase& database,
	HierarchySystem& hierarchySystem, AssetID prefabAsset, const std::unordered_map<UUID, PrefabBaseEntity>& oldBase) {

	if (!prefabAsset) {
		return false;
	}

	// 伝播対象のインスタンスごとに、所属シーンとルートを集める
	struct InstanceTarget {

		UUID sceneInstanceID{};
		Entity root = Entity::Null();
	};
	std::unordered_map<UUID, InstanceTarget> targets;
	PrefabOverrideUtility::SynchronizeNestedPrefabOwnership(world);
	world.ForEachAliveEntity([&](Entity entity) {

		if (!world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		const auto& link = world.GetComponent<PrefabLinkComponent>(entity);
		if (link.prefabAsset != prefabAsset) {
			return;
		}
		InstanceTarget& target = targets[link.prefabInstanceID];
		if (world.HasComponent<SceneObjectComponent>(entity)) {
			target.sceneInstanceID = world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID;
		}
		if (link.isPrefabRoot) {
			target.root = entity;
		}
		});

	// 再生成元のプレファブが読めない時は壊さず温存する、再生成失敗でインスタンスを失わないための前段ガード
	const auto prefabFullPath = database.ResolveFullPath(prefabAsset);
	if (prefabFullPath.empty()) {
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] 反映元のPrefabアセットが見つかりません AssetID={}", ToString(prefabAsset));
		return false;
	}
	const nlohmann::json prefabProbe = JsonAdapter::Load(prefabFullPath);
	if (!prefabProbe.is_object() || !prefabProbe.contains("Entities") ||
		!prefabProbe["Entities"].is_array() || prefabProbe["Entities"].empty()) {

		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] 反映元のPrefabデータが不正です path={}", prefabFullPath.string());
		return false;
	}

	// ベースをインスタンスと同じ表現へ正規化し、ラウンドトリップ由来の誤オーバーライド検出を防ぐ
	const std::unordered_map<UUID, PrefabBaseEntity> normalizedBase = NormalizeBaseForDiff(database, oldBase);

	// 全対象を破棄前に退避し、同一Prefabのネストでも親情報を失わないようにする
	struct TransactionTarget {

		UUID instanceID{};
		UUID sceneInstanceID{};
		PrefabInstanceData data;
		std::vector<InstanceEntityBackup> backup;
	};
	std::vector<TransactionTarget> transaction;
	transaction.reserve(targets.size());
	for (const auto& [instanceID, target] : targets) {

		if (!world.IsAlive(target.root)) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[Prefab] ルートを失ったPrefabインスタンスは反映できません InstanceID={}",
				ToString(instanceID));
			return false;
		}
		const auto& rootLink = world.GetComponent<PrefabLinkComponent>(target.root);
		if (rootLink.ownerPrefabInstanceID && targets.contains(rootLink.ownerPrefabInstanceID)) {
			continue;
		}

		TransactionTarget state{};
		state.instanceID = instanceID;
		state.sceneInstanceID = target.sceneInstanceID;
		state.data = PrefabOverrideUtility::CaptureInstance(world, database, instanceID, normalizedBase);
		state.data.prefabAsset = prefabAsset;
		PrefabInstanceData validated;
		if (!FromJson(ToJson(state.data), validated)) {
			Logger::Output(LogType::Engine, spdlog::level::err,
				"[Prefab] 対応表が不正なため伝播を中止します AssetID={} InstanceID={}",
				ToString(prefabAsset), ToString(instanceID));
			return false;
		}

		std::vector<Entity> ownedEntities;
		std::unordered_set<UUID> ownedInstances = { instanceID };
		std::unordered_set<uint64_t> collected;
		for (const Entity& member : PrefabOverrideUtility::CollectInstanceEntities(world, instanceID)) {
			CollectOwnedHierarchy(world, member, ownedInstances, ownedEntities, collected);
		}
		state.backup = CaptureInstanceBackup(world, ownedEntities);
		transaction.emplace_back(std::move(state));
	}
	std::sort(transaction.begin(), transaction.end(), [](const auto& lhs, const auto& rhs) {
		return lhs.instanceID.value < rhs.instanceID.value;
		});

	// 所有Entityだけを破棄し、別Prefabの子はその場に残す
	for (const TransactionTarget& state : transaction) {
		for (auto it = state.backup.rbegin(); it != state.backup.rend(); ++it) {

			const Entity entity = world.FindByUUID(it->stableUUID);
			if (world.IsAlive(entity)) {
				world.DestroyEntity(entity);
			}
		}
	}
	world.FlushPendingDestroyEntities();
	RebuildAllHierarchy(world, hierarchySystem);

	bool rebuilt = true;
	for (const TransactionTarget& state : transaction) {

		const Entity root = PrefabOverrideUtility::RebuildInstance(
			world, database, hierarchySystem, state.data, state.sceneInstanceID);
		if (!world.IsAlive(root)) {
			rebuilt = false;
			break;
		}
	}

	if (!rebuilt) {

		// 一部成功も含めて新しい実体を全て除去し、伝播前の状態へ戻す
		for (const TransactionTarget& state : transaction) {

			std::vector<Entity> created;
			std::unordered_set<UUID> ownedInstances = { state.instanceID };
			std::unordered_set<uint64_t> collected;
			for (const Entity& member : PrefabOverrideUtility::CollectInstanceEntities(world, state.instanceID)) {
				CollectOwnedHierarchy(world, member, ownedInstances, created, collected);
			}
			for (auto it = created.rbegin(); it != created.rend(); ++it) {
				if (world.IsAlive(*it)) {
					world.DestroyEntity(*it);
				}
			}
		}
		world.FlushPendingDestroyEntities();
		RebuildAllHierarchy(world, hierarchySystem);
		for (const TransactionTarget& state : transaction) {
			RestoreInstanceBackup(world, state.backup, state.sceneInstanceID);
		}
		RebuildAllHierarchy(world, hierarchySystem);
		Logger::Output(LogType::Engine, spdlog::level::err,
			"[Prefab] Prefab反映に失敗したため全インスタンスを元に戻しました AssetID={}",
			ToString(prefabAsset));
		return false;
	}

	RebuildAllHierarchy(world, hierarchySystem);
	return true;
}
