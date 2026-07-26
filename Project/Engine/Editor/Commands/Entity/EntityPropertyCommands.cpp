#include "EntityPropertyCommands.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

//============================================================================
//	RenameEntityCommand classMethods
//============================================================================
Engine::RenameEntityCommand::RenameEntityCommand(
	const Entity& targetEntity, const std::string_view& newName) :
	initialTarget_(targetEntity),
	newName_(newName) {
}

bool Engine::RenameEntityCommand::ApplyName(EditorCommandContext& context, const std::string& name) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	const Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	if (!world->HasComponent<NameComponent>(target)) {
		world->AddComponent<NameComponent>(target);
	}

	world->GetComponent<NameComponent>(target).name = name;
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::RenameEntityCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	if (!targetStableUUID_) {
		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent<NameComponent>(initialTarget_)) {
			oldName_ = world->GetComponent<NameComponent>(initialTarget_).name;
		} else {
			oldName_ = "Entity";
		}
		if (oldName_ == newName_) {
			return false;
		}
	}
	return ApplyName(context, newName_);
}

void Engine::RenameEntityCommand::Undo(EditorCommandContext& context) {

	ApplyName(context, oldName_);
}

bool Engine::RenameEntityCommand::Redo(EditorCommandContext& context) {

	return ApplyName(context, newName_);
}

//============================================================================
//	SetEntityActiveCommand classMethods
//============================================================================
Engine::SetEntityActiveCommand::SetEntityActiveCommand(const Entity& targetEntity, bool activeSelf) :
	initialTarget_(targetEntity),
	afterActiveSelf_(activeSelf) {
}

bool Engine::SetEntityActiveCommand::Apply(EditorCommandContext& context, bool activeSelf) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	const Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	if (!world->HasComponent<SceneObjectComponent>(target)) {
		auto& sceneObject = world->AddComponent<SceneObjectComponent>(target);
		sceneObject.localFileID = UUID::New();
		sceneObject.activeSelf = true;
		sceneObject.activeInHierarchy = true;
	}

	world->GetComponent<SceneObjectComponent>(target).activeSelf = activeSelf;

	HierarchySystem hierarchySystem;
	hierarchySystem.RefreshActiveTree(*world, target);
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetEntityActiveCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	if (!targetStableUUID_) {
		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent<SceneObjectComponent>(initialTarget_)) {
			beforeActiveSelf_ = world->GetComponent<SceneObjectComponent>(initialTarget_).activeSelf;
		} else {
			beforeActiveSelf_ = true;
		}
		if (beforeActiveSelf_ == afterActiveSelf_) {
			return false;
		}
	}
	return Apply(context, afterActiveSelf_);
}

void Engine::SetEntityActiveCommand::Undo(EditorCommandContext& context) {

	Apply(context, beforeActiveSelf_);
}

bool Engine::SetEntityActiveCommand::Redo(EditorCommandContext& context) {

	return Apply(context, afterActiveSelf_);
}

//============================================================================
//	SetEntityTagCommand classMethods
//============================================================================
Engine::SetEntityTagCommand::SetEntityTagCommand(const Entity& targetEntity, const std::string& tag) :
	initialTarget_(targetEntity),
	afterTag_(tag) {
}

bool Engine::SetEntityTagCommand::Apply(EditorCommandContext& context, const std::string& tag) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	const Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	if (!world->HasComponent<SceneObjectComponent>(target)) {
		auto& sceneObject = world->AddComponent<SceneObjectComponent>(target);
		sceneObject.localFileID = UUID::New();
	}

	world->GetComponent<SceneObjectComponent>(target).tag = tag;
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetEntityTagCommand::Execute(EditorCommandContext& context) {

	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	if (!targetStableUUID_) {
		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (world->HasComponent<SceneObjectComponent>(initialTarget_)) {
			beforeTag_ = world->GetComponent<SceneObjectComponent>(initialTarget_).tag;
		} else {
			beforeTag_ = "Untagged";
		}
		if (beforeTag_ == afterTag_) {
			return false;
		}
	}
	return Apply(context, afterTag_);
}

void Engine::SetEntityTagCommand::Undo(EditorCommandContext& context) {

	Apply(context, beforeTag_);
}

bool Engine::SetEntityTagCommand::Redo(EditorCommandContext& context) {

	return Apply(context, afterTag_);
}
