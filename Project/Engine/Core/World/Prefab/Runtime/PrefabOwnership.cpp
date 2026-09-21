#include "PrefabOwnership.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Serialization/PrefabHeader.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Prefab/PrefabLinkComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabDocument.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabOwnership.h>

// c++
#include <unordered_map>

using namespace Engine::PrefabDocument;

void Engine::PrefabOwnership::SetPrefabLink(ECSWorld& world, const Entity& entity, AssetID prefabAsset,
	UUID prefabLocalFileID, UUID prefabInstanceID, bool isPrefabRoot,
	UUID ownerPrefabInstanceID, UUID nestedSlotID, bool isPrefabAssetNested) {

	if (!world.IsAlive(entity)) {
		return;
	}
	auto& prefabLink = world.HasComponent<PrefabLinkComponent>(entity) ?
		world.GetComponent<PrefabLinkComponent>(entity) :
		world.AddComponent<PrefabLinkComponent>(entity);
	prefabLink.prefabAsset = prefabAsset;
	prefabLink.prefabLocalFileID = prefabLocalFileID;
	prefabLink.prefabInstanceID = prefabInstanceID;
	prefabLink.ownerPrefabInstanceID = ownerPrefabInstanceID;
	prefabLink.nestedSlotID = nestedSlotID;
	prefabLink.isPrefabAssetNested = isPrefabAssetNested;
	prefabLink.isPrefabRoot = isPrefabRoot;
}

Engine::UUID Engine::PrefabOwnership::SetPrefabLinkToSubtree(ECSWorld& world, const Entity& root,
	AssetID prefabAsset, UUID prefabInstanceID) {

	if (!world.IsAlive(root) || !prefabAsset) {
		return UUID{};
	}
	const UUID resolvedInstanceID = prefabInstanceID ? prefabInstanceID : UUID::New();
	auto linkSubtree = [&](auto&& self, const Entity& entity, bool isRoot) -> void {

		if (!world.IsAlive(entity)) {
			return;
		}
		if (!isRoot && world.HasComponent<PrefabLinkComponent>(entity)) {
			return;
		}
		if (world.HasComponent<SceneObjectComponent>(entity)) {

			const UUID prefabLocalFileID = world.GetComponent<SceneObjectComponent>(entity).localFileID;
			SetPrefabLink(world, entity, prefabAsset, prefabLocalFileID, resolvedInstanceID, isRoot, UUID{}, UUID{}, false);
		}
		if (!world.HasComponent<HierarchyComponent>(entity)) {
			return;
		}
		Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
		while (world.IsAlive(child)) {

			const Entity next = world.HasComponent<HierarchyComponent>(child) ?
				world.GetComponent<HierarchyComponent>(child).nextSibling : Entity::Null();
			self(self, child, false);
			child = next;
		}
		};
	linkSubtree(linkSubtree, root, true);
	return resolvedInstanceID;
}

bool Engine::PrefabOwnership::UnpackPrefabInstance(
	ECSWorld& world, const Entity& root, PrefabUnpackMode mode) {

	if (!world.IsAlive(root) || !world.HasComponent<PrefabLinkComponent>(root)) {
		return false;
	}
	const PrefabLinkComponent rootLink = world.GetComponent<PrefabLinkComponent>(root);
	if (!rootLink.isPrefabRoot || !rootLink.prefabInstanceID) {
		return false;
	}

	if (mode == PrefabUnpackMode::Completely) {

		const std::vector<Entity> entities = HierarchyUtility::CollectLogicalSubtree(world, root);
		for (const Entity& entity : entities) {

			if (world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity)) {
				world.RemoveComponent<PrefabLinkComponent>(entity);
			}
		}
		return true;
	}

	std::vector<Entity> instanceEntities;
	std::vector<std::pair<Entity, PrefabLinkComponent>> nestedUpdates;
	std::unordered_map<UUID, UUID> nestedSlots;
	world.ForEach<PrefabLinkComponent>([&](const Entity& entity, PrefabLinkComponent& link) {

		if (link.prefabInstanceID == rootLink.prefabInstanceID) {
			instanceEntities.emplace_back(entity);
			return;
		}
		if (link.ownerPrefabInstanceID != rootLink.prefabInstanceID) {
			return;
		}

		PrefabLinkComponent updated = link;
		updated.ownerPrefabInstanceID = rootLink.ownerPrefabInstanceID;
		if (rootLink.ownerPrefabInstanceID) {

			auto [it, inserted] = nestedSlots.try_emplace(link.prefabInstanceID, UUID{});
			if (inserted) {
				it->second = UUID::New();
			}
			updated.nestedSlotID = it->second;
			updated.isPrefabAssetNested = false;
		} else {

			updated.nestedSlotID = UUID{};
			updated.isPrefabAssetNested = false;
		}
		nestedUpdates.emplace_back(entity, updated);
		});

	for (const auto& [entity, link] : nestedUpdates) {

		if (world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity)) {
			world.GetComponent<PrefabLinkComponent>(entity) = link;
			world.MarkComponentModified<PrefabLinkComponent>(entity);
		}
	}
	for (const Entity& entity : instanceEntities) {

		if (world.IsAlive(entity) && world.HasComponent<PrefabLinkComponent>(entity)) {
			world.RemoveComponent<PrefabLinkComponent>(entity);
		}
	}
	return !instanceEntities.empty();
}
