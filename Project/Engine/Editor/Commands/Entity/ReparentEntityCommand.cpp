#include "ReparentEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Editor/Commands/Transform/TransformEditUtility.h>
#include <Engine/Editor/Utility/JointAttachmentEditor.h>
#include <Engine/Editor/Commands/Entity/EditorHierarchyPolicy.h>
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
Engine::ReparentEntityCommand::ReparentEntityCommand(const Entity& targetEntity, UUID newParentStableUUID)
	: initialTarget_(targetEntity) {

	newState_.parentStableUUID = newParentStableUUID;
}

Engine::ReparentEntityCommand::ReparentEntityCommand(
	const Entity& targetEntity, const Entity& newSkinnedEntity, std::string newJointName)
	: initialTarget_(targetEntity), initialSkinnedEntity_(newSkinnedEntity) {

	newState_.jointName = std::move(newJointName);
	newState_.jointAttached = true;
}

bool Engine::ReparentEntityCommand::CaptureState(ECSWorld& world, const Entity& entity, ParentState& state) const {

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

		const Entity skinnedEntity =
			SceneObjectUtility::FindByLocalFileID(*world, state.sceneInstanceID, state.skinnedLocalFileID);
		if (!world->IsAlive(skinnedEntity) || state.jointName.empty()) {
			return false;
		}
		JointAttachmentEditor::Attach(*world, hierarchySystem, child, skinnedEntity, state.jointName);
		if (!world->HasComponent<JointAttachmentComponent>(child)) {
			return false;
		}
		const auto& attachment = world->GetComponent<JointAttachmentComponent>(child);
		if (attachment.skinnedEntityLocalFileID != state.skinnedLocalFileID || attachment.jointName != state.jointName) {
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
		if (!EditorHierarchyPolicy::CanSetParent(context.editorContext, *world, child, newParent)) {
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

			if (!world->IsAlive(initialSkinnedEntity_) || !world->HasComponent<SceneObjectComponent>(initialSkinnedEntity_)) {
				return false;
			}
			newState_.skinnedLocalFileID = world->GetComponent<SceneObjectComponent>(initialSkinnedEntity_).localFileID;
			newState_.sceneInstanceID = world->GetComponent<SceneObjectComponent>(initialSkinnedEntity_).sceneInstanceID;
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
		return lhs.skinnedLocalFileID == rhs.skinnedLocalFileID && lhs.sceneInstanceID == rhs.sceneInstanceID &&
			   lhs.jointName == rhs.jointName;
	}
	return lhs.parentStableUUID == rhs.parentStableUUID;
}
