#include "ReparentEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/Utility/JointAttachmentEditor.h>
#include <Engine/Editor/Utility/PrefabInstanceEditUtility.h>
#include <Engine/Core/World/Components/Animation/JointAttachmentComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

// c++
#include <algorithm>
#include <utility>

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
	initialTarget_(targetEntity) {

	newState_.parentStableUUID = newParentStableUUID;
}

Engine::ReparentEntityCommand::ReparentEntityCommand(
	const Entity& targetEntity, const Entity& newSkinnedEntity, std::string newJointName) :
	initialTarget_(targetEntity), initialSkinnedEntity_(newSkinnedEntity) {

	newState_.jointName = std::move(newJointName);
	newState_.jointAttached = true;
}

bool Engine::ReparentEntityCommand::CaptureState(ECSWorld& world,
	const Entity& entity, ParentState& state) const {

	if (!world.IsAlive(entity)) {
		return false;
	}
	state = ParentState{};
	if (world.HasComponent<HierarchyComponent>(entity)) {

		const Entity parent = world.GetComponent<HierarchyComponent>(entity).parent;
		if (world.IsAlive(parent)) {
			state.parentStableUUID = world.GetUUID(parent);
		}
	}
	if (world.HasComponent<JointAttachmentComponent>(entity)) {

		const auto& attachment = world.GetComponent<JointAttachmentComponent>(entity);
		state.skinnedLocalFileID = attachment.skinnedEntityLocalFileID;
		if (world.HasComponent<SceneObjectComponent>(entity)) {
			state.sceneInstanceID = world.GetComponent<SceneObjectComponent>(entity).sceneInstanceID;
		}
		state.jointName = attachment.jointName;
		state.jointAttached = true;
	}
	if (world.HasComponent<TransformComponent>(entity)) {

		state.transform = world.GetComponent<TransformComponent>(entity);
		state.hasTransform = true;
	}
	return true;
}

bool Engine::ReparentEntityCommand::ApplyState(EditorCommandContext& context, const ParentState& state) {

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

	HierarchySystem hierarchySystem{};
	if (state.jointAttached) {

		const Entity skinnedEntity = SceneObjectUtility::FindByLocalFileID(
			*world, state.sceneInstanceID, state.skinnedLocalFileID);
		if (!world->IsAlive(skinnedEntity) || state.jointName.empty()) {
			return false;
		}
		JointAttachmentEditor::Attach(*world, hierarchySystem, child, skinnedEntity, state.jointName);
		if (!world->HasComponent<JointAttachmentComponent>(child)) {
			return false;
		}
		const auto& attachment = world->GetComponent<JointAttachmentComponent>(child);
		if (attachment.skinnedEntityLocalFileID != state.skinnedLocalFileID ||
			attachment.jointName != state.jointName) {
			return false;
		}
	} else {

		// 新しい親を検索しUUIDが無効な場合はNullエンティティになる
		Entity newParent = Entity::Null();
		if (state.parentStableUUID) {

			newParent = world->FindByUUID(state.parentStableUUID);
			if (!world->IsAlive(newParent)) {
				return false;
			}
		}
		if (!PrefabInstanceEditUtility::CanChangeParent(context.editorContext, *world, child, newParent)) {
			return false;
		}

		// ジョイント親子付けを解除して通常階層へ戻す
		JointAttachmentEditor::Detach(*world, child);
		hierarchySystem.SetParent(*world, child, newParent);
	}
	if (state.hasTransform) {
		TransformEditUtility::ApplyImmediate(*world, child, state.transform);
	}
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

	// 初回実行時のみ現在の親を保存する
	if (!initialized_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		// 対象のUUIDを保存する
		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (!CaptureState(*world, initialTarget_, oldState_)) {
			return false;
		}
		if (newState_.jointAttached) {

			if (!world->IsAlive(initialSkinnedEntity_) ||
				!world->HasComponent<SceneObjectComponent>(initialSkinnedEntity_)) {
				return false;
			}
			newState_.skinnedLocalFileID =
				world->GetComponent<SceneObjectComponent>(initialSkinnedEntity_).localFileID;
			newState_.sceneInstanceID =
				world->GetComponent<SceneObjectComponent>(initialSkinnedEntity_).sceneInstanceID;
			if (!newState_.skinnedLocalFileID) {
				return false;
			}
		}

		// 変化がないなら履歴に積まない
		if (IsSameParent(oldState_, newState_)) {
			return false;
		}
		if (!ApplyState(context, newState_)) {
			return false;
		}
		const Entity child = world->FindByUUID(targetStableUUID_);
		if (!world->IsAlive(child)) {
			return false;
		}
		if (world->HasComponent<TransformComponent>(child)) {
			newState_.transform = world->GetComponent<TransformComponent>(child);
			newState_.hasTransform = true;
		}
		initialized_ = true;
		return true;
	}
	return ApplyState(context, newState_);
}

void Engine::ReparentEntityCommand::Undo(EditorCommandContext& context) {

	ApplyState(context, oldState_);
}

bool Engine::ReparentEntityCommand::Redo(EditorCommandContext& context) {

	return ApplyState(context, newState_);
}

bool Engine::ReparentEntityCommand::IsSameParent(const ParentState& lhs, const ParentState& rhs) const {

	if (lhs.jointAttached != rhs.jointAttached) {
		return false;
	}
	if (lhs.jointAttached) {
		return lhs.skinnedLocalFileID == rhs.skinnedLocalFileID &&
			lhs.sceneInstanceID == rhs.sceneInstanceID && lhs.jointName == rhs.jointName;
	}
	return lhs.parentStableUUID == rhs.parentStableUUID;
}

//============================================================================
//	ReparentEntitiesCommand classMethods
//============================================================================
Engine::ReparentEntitiesCommand::ReparentEntitiesCommand(
	std::vector<Entity> targetEntities, UUID newParentStableUUID) :
	targetEntities_(std::move(targetEntities)),
	newParentStableUUID_(newParentStableUUID) {
}

bool Engine::ReparentEntitiesCommand::Execute(EditorCommandContext& context) {

	if (initialized_) {
		return Redo(context);
	}
	if (targetEntities_.empty()) {
		return false;
	}

	commands_.reserve(targetEntities_.size());
	for (const Entity& entity : targetEntities_) {
		commands_.emplace_back(
			std::make_unique<ReparentEntityCommand>(entity, newParentStableUUID_));
	}

	size_t executedCount = 0;
	for (const std::unique_ptr<ReparentEntityCommand>& command : commands_) {
		if (!command->Execute(context)) {
			for (size_t i = executedCount; i > 0; --i) {
				commands_[i - 1]->Undo(context);
			}
			RestoreSelection(context);
			return false;
		}
		++executedCount;
	}
	initialized_ = true;
	RestoreSelection(context);
	return true;
}

void Engine::ReparentEntitiesCommand::Undo(EditorCommandContext& context) {

	for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
		(*it)->Undo(context);
	}
	RestoreSelection(context);
}

bool Engine::ReparentEntitiesCommand::Redo(EditorCommandContext& context) {

	size_t redoneCount = 0;
	for (const std::unique_ptr<ReparentEntityCommand>& command : commands_) {
		if (!command->Redo(context)) {
			for (size_t i = redoneCount; i > 0; --i) {
				commands_[i - 1]->Undo(context);
			}
			RestoreSelection(context);
			return false;
		}
		++redoneCount;
	}
	RestoreSelection(context);
	return true;
}

void Engine::ReparentEntitiesCommand::RestoreSelection(
	EditorCommandContext& context) const {

	if (context.editorState) {
		context.editorState->SetSelectedEntities(targetEntities_);
	}
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
	const Entity target = world->FindByUUID(targetStableUUID_);
	const auto anchorIt = std::find_if(entities.begin(), entities.end(), [&](const Entity& entity) {
		return entity != target;
		});
	if (anchorIt == entities.end() ||
		!PrefabInstanceEditUtility::CanChangeSiblingOrder(context.editorContext, *world, target, *anchorIt)) {
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
