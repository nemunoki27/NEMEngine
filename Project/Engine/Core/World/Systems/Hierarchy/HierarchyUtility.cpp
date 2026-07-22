#include "HierarchyUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>

// c++
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace {

	struct LocalKey {

		Engine::UUID sceneInstanceID{};
		Engine::UUID localFileID{};

		bool operator==(const LocalKey& rhs) const noexcept {
			return sceneInstanceID == rhs.sceneInstanceID && localFileID == rhs.localFileID;
		}
	};

	struct LocalKeyHash {

		size_t operator()(const LocalKey& key) const noexcept {
			const size_t h1 = std::hash<Engine::UUID>{}(key.sceneInstanceID);
			const size_t h2 = std::hash<Engine::UUID>{}(key.localFileID);
			return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
		}
	};

	uint64_t EntityKey(const Engine::Entity& entity) {

		return (static_cast<uint64_t>(entity.generation) << 32) | entity.index;
	}
}

namespace Engine::HierarchyUtility {

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

	bool IsRoot(ECSWorld& world, Entity entity) {
		if (!world.IsAlive(entity)) { return true; }
		if (const auto* hierarchy = world.TryGetComponent<HierarchyComponent>(entity)) {
			return !world.IsAlive(hierarchy->parent);
		}
		return true;
	}

	std::vector<Entity> CollectLogicalSubtree(ECSWorld& world, Entity root) {

		std::vector<Entity> entities;
		if (!world.IsAlive(root)) {
			return entities;
		}

		// ジョイント接続先をシーンとローカルIDの組み合わせから引けるようにする
		std::unordered_multimap<LocalKey, Entity, LocalKeyHash> attachedBySkinned;
		attachedBySkinned.reserve(world.GetRecordCount());
		world.ForEachAliveEntity([&](Entity entity) {

			if (!world.HasComponent<JointAttachmentComponent>(entity) ||
				!world.HasComponent<SceneObjectComponent>(entity)) {
				return;
			}
			const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
			if (!attachment.skinnedEntityLocalFileID) {
				return;
			}
			const auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			attachedBySkinned.emplace(
				LocalKey{ sceneObject.sceneInstanceID, attachment.skinnedEntityLocalFileID }, entity);
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
				const auto [begin, end] = attachedBySkinned.equal_range(
					LocalKey{ sceneObject.sceneInstanceID, sceneObject.localFileID });
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
				child = world.HasComponent<HierarchyComponent>(child) ?
					world.GetComponent<HierarchyComponent>(child).nextSibling : Entity::Null();
			}
		}
		return entities;
	}

} // Engine::HierarchyUtility
