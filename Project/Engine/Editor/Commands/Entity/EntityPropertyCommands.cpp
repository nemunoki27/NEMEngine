#include "EntityPropertyCommands.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Components/Scene/NameComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>

//============================================================================
//	RenameEntityCommand classMethods
//============================================================================
Engine::RenameEntityCommand::RenameEntityCommand(const Entity& targetEntity, std::string_view newName) :
	initialTarget_(targetEntity),
	newName_(newName) {
}

bool Engine::RenameEntityCommand::ApplyName(EditorCommandContext& context, const std::string& name) {

	// 編集対象のWorldを確認
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	// 初回の対象と変更前の値を保存
	if (!targetStableUUID_) {
		return false;
	}

	// 保存したUUIDで現在のEntityを検索
	const Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	if (!world->HasComponent<NameComponent>(target)) {
		world->AddComponent<NameComponent>(target);
	}

	// Entity名を更新
	world->GetComponent<NameComponent>(target).name = name;
	// 操作したEntityを選択
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::RenameEntityCommand::Execute(EditorCommandContext& context) {

	// 編集対象のWorldを確認
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	// 初回の対象と変更前の値を保存
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

	// 編集対象のWorldを確認
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	// 初回の対象と変更前の値を保存
	if (!targetStableUUID_) {
		return false;
	}

	// 保存したUUIDで現在のEntityを検索
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

	// 子孫を含めた有効状態を更新
	SceneObjectUtility::SetActiveSelf(*world, target, activeSelf);
	// 操作したEntityを選択
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetEntityActiveCommand::Execute(EditorCommandContext& context) {

	// 編集対象のWorldを確認
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	// 初回の対象と変更前の値を保存
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

	// 編集対象のWorldを確認
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	// 初回の対象と変更前の値を保存
	if (!targetStableUUID_) {
		return false;
	}

	// 保存したUUIDで現在のEntityを検索
	const Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	if (!world->HasComponent<SceneObjectComponent>(target)) {
		auto& sceneObject = world->AddComponent<SceneObjectComponent>(target);
		sceneObject.localFileID = UUID::New();
	}

	// Entityのタグを更新
	world->GetComponent<SceneObjectComponent>(target).tag = tag;
	// 操作したEntityを選択
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetEntityTagCommand::Execute(EditorCommandContext& context) {

	// 編集対象のWorldを確認
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	// 初回の対象と変更前の値を保存
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
