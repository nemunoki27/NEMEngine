#include "HierarchyUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <vector>
#include <algorithm>

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

} // Engine::HierarchyUtility
