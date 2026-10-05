#include "ReorderEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include "EditorHierarchyPolicy.h"
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

// c++
#include <algorithm>

namespace {

	// 保存された兄弟順を取得する
	int32_t GetSiblingOrder(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return 0;
		}
		return world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder;
	}

	// 同じ親かSceneのRootを列挙する
	std::vector<Engine::Entity> CollectSiblingEntities(
		Engine::ECSWorld& world, const Engine::Entity& parent, const Engine::Entity& target) {

		std::vector<Engine::Entity> entities;
		if (world.IsAlive(parent) && world.HasComponent<Engine::HierarchyComponent>(parent)) {

			for (Engine::Entity child = world.GetComponent<Engine::HierarchyComponent>(parent).firstChild;
				child.IsValid() && world.IsAlive(child);) {

				entities.emplace_back(child);
				if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
					break;
				}
				child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
			}
			return entities;
		}

		entities.reserve(world.GetRecordCount());
		const auto* membership = world.TryGetComponent<Engine::SceneObjectComponent>(target);
		const Engine::UUID scene = membership ? membership->sceneInstanceID : Engine::UUID{};
		world.ForEachAliveEntity([&](Engine::Entity entity) {
			// 別SceneのRootを兄弟順の変更へ巻き込まない
			if (membership) {
				const auto* candidate = world.TryGetComponent<Engine::SceneObjectComponent>(entity);
				if (!candidate || candidate->sceneInstanceID != scene) {
					return;
				}
			}

			if (Engine::HierarchyUtility::IsRoot(world, entity)) {

				entities.emplace_back(entity);
			}
		});
		std::stable_sort(entities.begin(), entities.end(), [&](const Engine::Entity& lhs, const Engine::Entity& rhs) {
			return GetSiblingOrder(world, lhs) < GetSiblingOrder(world, rhs);
		});
		return entities;
	}

	// Undo用の順序をUUIDで保持する
	std::vector<Engine::UUID> ToStableUUIDOrder(Engine::ECSWorld& world, const std::vector<Engine::Entity>& entities) {

		std::vector<Engine::UUID> order;
		order.reserve(entities.size());
		for (const Engine::Entity& entity : entities) {

			order.emplace_back(world.GetUUID(entity));
		}
		return order;
	}

	// 対象UUIDが順序に含まれるか確認する
	bool ContainsUUID(const std::vector<Engine::UUID>& values, Engine::UUID value) {

		return std::find(values.begin(), values.end(), value) != values.end();
	}
}

Engine::ReorderEntityCommand::ReorderEntityCommand(const Entity& targetEntity, const Entity& anchorEntity, bool insertAfter)
	: initialTarget_(targetEntity), initialAnchor_(anchorEntity), insertAfter_(insertAfter) {
}

bool Engine::ReorderEntityCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	if (oldOrder_.empty()) {

		if (!world->IsAlive(initialTarget_) || !world->IsAlive(initialAnchor_) || initialTarget_ == initialAnchor_) {
			return false;
		}

		Entity targetParent = HierarchyUtility::GetParent(*world, initialTarget_);
		Entity anchorParent = HierarchyUtility::GetParent(*world, initialAnchor_);
		if (targetParent != anchorParent) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		parentStableUUID_ = world->IsAlive(targetParent) ? world->GetUUID(targetParent) : UUID{};
		oldOrder_ = ToStableUUIDOrder(*world, CollectSiblingEntities(*world, targetParent, initialTarget_));
		if (!ContainsUUID(oldOrder_, targetStableUUID_)) {
			return false;
		}

		const UUID anchorStableUUID = world->GetUUID(initialAnchor_);
		newOrder_ = oldOrder_;
		newOrder_.erase(std::remove(newOrder_.begin(), newOrder_.end(), targetStableUUID_), newOrder_.end());

		auto anchorIt = std::find(newOrder_.begin(), newOrder_.end(), anchorStableUUID);
		if (anchorIt == newOrder_.end()) {
			return false;
		}
		if (insertAfter_) {
			++anchorIt;
		}
		newOrder_.insert(anchorIt, targetStableUUID_);
		if (newOrder_ == oldOrder_) {
			return false;
		}
	}
	return ApplyOrder(context, newOrder_);
}

void Engine::ReorderEntityCommand::Undo(EditorCommandContext& context) {

	ApplyOrder(context, oldOrder_);
}

bool Engine::ReorderEntityCommand::Redo(EditorCommandContext& context) {

	return ApplyOrder(context, newOrder_);
}

bool Engine::ReorderEntityCommand::ApplyOrder(EditorCommandContext& context, const std::vector<UUID>& order) {

	if (!context.CanEditScene() || order.empty()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	Entity parent = Entity::Null();
	if (parentStableUUID_) {

		parent = world->FindByUUID(parentStableUUID_);
		if (!world->IsAlive(parent)) {
			return false;
		}
		if (!world->HasComponent<HierarchyComponent>(parent)) {
			world->AddComponent<HierarchyComponent>(parent);
		}
	}

	std::vector<Entity> entities;
	entities.reserve(order.size());
	for (UUID stableUUID : order) {

		Entity entity = world->FindByUUID(stableUUID);
		if (!world->IsAlive(entity)) {
			continue;
		}
		if (!world->HasComponent<HierarchyComponent>(entity)) {
			world->AddComponent<HierarchyComponent>(entity);
		}
		if (HierarchyUtility::GetParent(*world, entity) != parent) {
			return false;
		}
		entities.emplace_back(entity);
	}
	if (entities.empty()) {
		return false;
	}
	const Entity target = world->FindByUUID(targetStableUUID_);
	const auto anchorIt =
		std::find_if(entities.begin(), entities.end(), [&](const Entity& entity) { return entity != target; });
	if (anchorIt == entities.end() || !EditorHierarchyPolicy::CanReorder(context.editorContext, *world, target, *anchorIt)) {
		return false;
	}

	if (world->IsAlive(parent)) {

		auto& parentHierarchy = world->GetComponent<HierarchyComponent>(parent);
		parentHierarchy.firstChild = entities.front();
		parentHierarchy.lastChild = entities.back();
	} else {

		std::stable_sort(entities.begin(), entities.end(), [&](const Entity& lhs, const Entity& rhs) {
			auto lhsIt = std::find(order.begin(), order.end(), world->GetUUID(lhs));
			auto rhsIt = std::find(order.begin(), order.end(), world->GetUUID(rhs));
			return lhsIt < rhsIt;
		});
	}

	for (size_t i = 0; i < entities.size(); ++i) {

		auto& hierarchy = world->GetComponent<HierarchyComponent>(entities[i]);
		hierarchy.parent = parent;
		hierarchy.prevSibling = (world->IsAlive(parent) && i != 0) ? entities[i - 1] : Entity::Null();
		hierarchy.nextSibling = (world->IsAlive(parent) && i + 1 < entities.size()) ? entities[i + 1] : Entity::Null();
		hierarchy.siblingOrder = static_cast<int32_t>(i);
		if (!world->IsAlive(parent)) {

			hierarchy.parentLocalFileID = UUID{};
		}
	}

	if (context.editorState) {

		context.editorState->SelectEntity(world->FindByUUID(targetStableUUID_));
	}
	return true;
}
