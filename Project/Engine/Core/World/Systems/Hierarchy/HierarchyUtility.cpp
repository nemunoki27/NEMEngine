#include "HierarchyUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneEntityKey.h>

// c++
#include <vector>
#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace {

	// Entityの世代と番号から重複判定用のキーを作る
	uint64_t EntityKey(const Engine::Entity& entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}
}

namespace Engine::HierarchyUtility {

	Entity GetParent(const ECSWorld& world, Entity entity) {

		if (const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(entity)) {
			return world.IsAlive(hierarchy->parent) ? hierarchy->parent : Entity::Null();
		}
		return Entity::Null();
	}

	bool CanSetParent(const ECSWorld& world, Entity child, Entity parent) {

		if (!world.IsAlive(child)) {
			return false;
		}
		size_t remaining = world.GetRecordCount() + 1;
		// 壊れた階層でも無限に親をたどらない
		while (world.IsAlive(parent)) {
			if (parent == child || remaining-- == 0) {
				return false;
			}
			parent = GetParent(world, parent);
		}
		return true;
	}

	void SortChildLinksBySiblingOrder(ECSWorld& world, Entity parent) {

		if (!world.IsAlive(parent) || !world.HasComponent<HierarchyComponent>(parent)) {
			return;
		}

		auto& parentHierarchy = world.GetComponent<HierarchyComponent>(parent);
		std::vector<Entity> children;
		for (Entity child = parentHierarchy.firstChild; child.IsValid() && world.IsAlive(child);) {

			children.emplace_back(child);
			if (!world.HasComponent<HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<HierarchyComponent>(child).nextSibling;
		}
		if (children.size() < 2) {
			return;
		}

		std::stable_sort(children.begin(), children.end(), [&](const Entity& lhs, const Entity& rhs) {
			return world.GetComponent<HierarchyComponent>(lhs).siblingOrder <
				   world.GetComponent<HierarchyComponent>(rhs).siblingOrder;
		});

		parentHierarchy.firstChild = children.front();
		parentHierarchy.lastChild = children.back();
		for (size_t i = 0; i < children.size(); ++i) {

			auto& childHierarchy = world.GetComponent<HierarchyComponent>(children[i]);
			childHierarchy.prevSibling = (i == 0) ? Entity::Null() : children[i - 1];
			childHierarchy.nextSibling = (i + 1 < children.size()) ? children[i + 1] : Entity::Null();
			childHierarchy.siblingOrder = static_cast<int32_t>(i);
		}
	}

	bool IsRoot(const ECSWorld& world, Entity entity) {

		if (!world.IsAlive(entity)) {
			return true;
		}
		if (const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(entity)) {
			return !world.IsAlive(hierarchy->parent);
		}
		return true;
	}

	int32_t FindMaxRootSiblingOrder(const ECSWorld& world, Entity exclude) {

		int32_t maximum = -1;
		// 親が生存していないEntityだけを比較する
		world.ForEachAliveEntity([&](Entity entity) {
			if (entity == exclude) {
				return;
			}
			const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(entity);
			if (hierarchy && !world.IsAlive(hierarchy->parent)) {
				maximum = (std::max)(maximum, hierarchy->siblingOrder);
			}
		});
		return maximum;
	}

	bool TryGetNextRootSiblingOrder(const ECSWorld& world, Entity exclude, int32_t& order) {

		const int32_t maximum = FindMaxRootSiblingOrder(world, exclude);
		// 既存Entityの並びを変えず上限到達を返す
		if (maximum == std::numeric_limits<int32_t>::max()) {
			return false;
		}
		order = maximum + 1;
		return true;
	}

	std::vector<Entity> CollectLogicalRoots(ECSWorld& world, std::span<const Entity> selection) {

		std::vector<Entity> candidates;
		std::unordered_set<uint64_t> selected;
		for (const Entity& entity : selection) {
			if (world.IsAlive(entity) && selected.emplace(EntityKey(entity)).second) {
				candidates.emplace_back(entity);
			}
		}
		if (candidates.size() < 2) {
			return candidates;
		}

		// Jointの接続先を一度の走査で索引化する
		std::unordered_map<SceneEntityKey, Entity, SceneEntityKeyHash> entities;
		world.ForEachAliveEntity([&](Entity entity) {
			if (const auto* membership = world.TryGetComponent<SceneObjectComponent>(entity)) {
				if (membership->localFileID) {
					const auto [entry, inserted] =
						entities.emplace(SceneEntityKey{membership->sceneInstanceID, membership->localFileID}, entity);
					if (!inserted) {
						entry->second = Entity::Null();
					}
				}
			}
		});
		const auto parentOf = [&](Entity entity) {
			if (const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(entity)) {
				if (world.IsAlive(hierarchy->parent)) {
					return hierarchy->parent;
				}
			}
			const auto* joint = world.TryGetComponent<JointAttachmentComponent>(entity);
			const auto* membership = world.TryGetComponent<SceneObjectComponent>(entity);
			if (joint && membership && joint->skinnedEntityLocalFileID) {
				const auto parent = entities.find({membership->sceneInstanceID, joint->skinnedEntityLocalFileID});
				if (parent != entities.end()) {
					return parent->second;
				}
			}
			return Entity::Null();
		};

		std::vector<Entity> roots;
		std::unordered_set<uint64_t> visited;
		for (const Entity& candidate : candidates) {
			visited.clear();
			bool nested = false;
			Entity parent = parentOf(candidate);
			while (world.IsAlive(parent) && visited.emplace(EntityKey(parent)).second) {
				if (selected.contains(EntityKey(parent))) {
					nested = true;
					break;
				}
				parent = parentOf(parent);
			}
			if (!nested) {
				roots.emplace_back(candidate);
			}
		}
		return roots;
	}

	std::vector<Entity> CollectLogicalSubtree(ECSWorld& world, Entity root) {

		std::vector<Entity> entities;
		if (!world.IsAlive(root)) {
			return entities;
		}

		// ジョイント接続先をシーンとローカルIDの組み合わせから引けるようにする
		std::unordered_multimap<SceneEntityKey, Entity, SceneEntityKeyHash> attachedBySkinned;
		attachedBySkinned.reserve(world.GetRecordCount());
		world.ForEachAliveEntity([&](Entity entity) {
			if (!world.HasComponent<JointAttachmentComponent>(entity) || !world.HasComponent<SceneObjectComponent>(entity)) {
				return;
			}
			const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
			if (!attachment.skinnedEntityLocalFileID) {
				return;
			}
			const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			attachedBySkinned.emplace(SceneEntityKey{sceneObject.sceneInstanceID, attachment.skinnedEntityLocalFileID}, entity);
		});

		std::vector<Entity> stack;
		std::unordered_set<uint64_t> collected;
		stack.emplace_back(root);
		while (!stack.empty()) {

			const Entity entity = stack.back();
			stack.pop_back();
			if (!world.IsAlive(entity) || !collected.emplace(EntityKey(entity)).second) {
				continue;
			}

			entities.emplace_back(entity);
			if (world.HasComponent<SceneObjectComponent>(entity)) {

				const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
				const auto [begin, end] =
					attachedBySkinned.equal_range(SceneEntityKey{sceneObject.sceneInstanceID, sceneObject.localFileID});
				for (auto it = begin; it != end; ++it) {
					stack.emplace_back(it->second);
				}
			}

			if (!world.HasComponent<HierarchyComponent>(entity)) {
				continue;
			}
			Entity child = world.GetComponent<HierarchyComponent>(entity).firstChild;
			while (world.IsAlive(child)) {

				stack.emplace_back(child);
				child = world.HasComponent<HierarchyComponent>(child)
							? world.GetComponent<HierarchyComponent>(child).nextSibling
							: Entity::Null();
			}
		}
		return entities;
	}

} // Engine::HierarchyUtility
