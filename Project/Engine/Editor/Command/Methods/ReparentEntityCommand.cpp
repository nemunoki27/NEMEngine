#include "ReparentEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>

// c++
#include <algorithm>

//============================================================================
//	ReparentEntityCommand classMethods
//============================================================================

namespace {

	Engine::Entity GetParent(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return Engine::Entity::Null();
		}
		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		return world.IsAlive(hierarchy.parent) ? hierarchy.parent : Engine::Entity::Null();
	}

	bool IsRootEntity(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return true;
		}
		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		return !world.IsAlive(hierarchy.parent);
	}

	int32_t GetSiblingOrder(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return 0;
		}
		return world.GetComponent<Engine::HierarchyComponent>(entity).siblingOrder;
	}

	std::vector<Engine::Entity> CollectSiblingEntities(Engine::ECSWorld& world, const Engine::Entity& parent) {

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
		world.ForEachAliveEntity([&](Engine::Entity entity) {
			if (IsRootEntity(world, entity)) {

				entities.emplace_back(entity);
			}
			});
		std::stable_sort(entities.begin(), entities.end(), [&](const Engine::Entity& lhs, const Engine::Entity& rhs) {
			return GetSiblingOrder(world, lhs) < GetSiblingOrder(world, rhs);
			});
		return entities;
	}

	std::vector<Engine::UUID> ToStableUUIDOrder(Engine::ECSWorld& world, const std::vector<Engine::Entity>& entities) {

		std::vector<Engine::UUID> order;
		order.reserve(entities.size());
		for (const Engine::Entity& entity : entities) {

			order.emplace_back(world.GetUUID(entity));
		}
		return order;
	}

	bool ContainsUUID(const std::vector<Engine::UUID>& values, Engine::UUID value) {

		return std::find(values.begin(), values.end(), value) != values.end();
	}
}

Engine::ReparentEntityCommand::ReparentEntityCommand(const Entity& targetEntity, UUID newParentStableUUID) :
	initialTarget_(targetEntity), newParentStableUUID_(newParentStableUUID) {
}

bool Engine::ReparentEntityCommand::ApplyParent(EditorCommandContext& context, UUID parentStableUUID) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	// UUIDからエンティティを検索する
	Entity child = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(child)) {
		return false;
	}

	// 新しい親を検索する。UUIDが無効な場合はNullエンティティになる
	Entity newParent = Entity::Null();
	if (parentStableUUID) {

		newParent = world->FindByUUID(parentStableUUID);
		if (!world->IsAlive(newParent)) {
			return false;
		}
	}

	// 親子関係を更新する
	HierarchySystem hierarchySystem;
	hierarchySystem.SetParent(*world, child, newParent);
	if (context.editorState) {

		context.editorState->SelectEntity(child);
	}
	return true;
}

bool Engine::ReparentEntityCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行時のみ現在の親を保存
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		// 対象のUUIDを保存する
		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent<HierarchyComponent>(initialTarget_)) {

			const auto& hierarchy = world->GetComponent<HierarchyComponent>(initialTarget_);
			if (world->IsAlive(hierarchy.parent)) {
				oldParentStableUUID_ = world->GetUUID(hierarchy.parent);
			}
		}

		// 変化がないなら履歴に積まない
		if (oldParentStableUUID_ == newParentStableUUID_) {
			return false;
		}
	}
	return ApplyParent(context, newParentStableUUID_);
}

void Engine::ReparentEntityCommand::Undo(EditorCommandContext& context) {

	ApplyParent(context, oldParentStableUUID_);
}

bool Engine::ReparentEntityCommand::Redo(EditorCommandContext& context) {

	return ApplyParent(context, newParentStableUUID_);
}

Engine::ReorderEntityCommand::ReorderEntityCommand(const Entity& targetEntity, const Entity& anchorEntity, bool insertAfter) :
	initialTarget_(targetEntity), initialAnchor_(anchorEntity), insertAfter_(insertAfter) {
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

		Entity targetParent = GetParent(*world, initialTarget_);
		Entity anchorParent = GetParent(*world, initialAnchor_);
		if (targetParent != anchorParent) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		parentStableUUID_ = world->IsAlive(targetParent) ? world->GetUUID(targetParent) : UUID{};
		oldOrder_ = ToStableUUIDOrder(*world, CollectSiblingEntities(*world, targetParent));
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
		if (GetParent(*world, entity) != parent) {
			return false;
		}
		entities.emplace_back(entity);
	}
	if (entities.empty()) {
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
