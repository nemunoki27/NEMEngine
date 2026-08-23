#include "AddScriptEntryCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Scripting/ScriptComponent.h>
#include <Engine/Editor/Core/EditorState.h>

//============================================================================
//	AddScriptEntryCommand classMethods
//============================================================================
Engine::AddScriptEntryCommand::AddScriptEntryCommand(const Entity& targetEntity,
	const std::string_view& scriptTypeID, const std::string_view& typeName,
	AssetID scriptAsset) :
	initialTarget_(targetEntity),
	scriptTypeID_(scriptTypeID),
	typeName_(typeName),
	scriptAsset_(scriptAsset) {
}

bool Engine::AddScriptEntryCommand::ApplyAdd(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}
	if (scriptTypeID_.empty() || typeName_.empty()) {
		return false;
	}

	// UUIDからエンティティを検索する
	Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	// ScriptComponentが無い場合は追加してからScriptEntryを積む
	if (!world->HasComponent<ScriptComponent>(target)) {
		if (!world->AddComponentByName(target, ScriptComponent::kTypeName)) {
			return false;
		}
	}

	// Stable Script Type IDを持つ解決済みスロットとして追加する
	world->GetBuffer<ScriptEntry>(target).Add(
		MakeScriptEntry(scriptTypeID_, typeName_, scriptAsset_));
	world->MarkComponentModified<ScriptComponent>(target);
	world->MarkComponentModified<ScriptEntry>(target);

	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::AddScriptEntryCommand::ApplyRestore(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	// UUIDからエンティティを検索する
	Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	if (createdComponent_) {

		world->RemoveComponentByName(target, ScriptComponent::kTypeName);
	} else {

		world->AddComponentFromJson(target, ScriptComponent::kTypeName, beforeData_);
	}

	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::AddScriptEntryCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行なら、対象エンティティのUUIDと追加前のScriptComponentを保存する
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		createdComponent_ = !world->HasComponent<ScriptComponent>(initialTarget_);
		if (!createdComponent_) {
			if (!world->SerializeComponentToJson(
				initialTarget_, ScriptComponent::kTypeName, beforeData_)) {
				return false;
			}
		}
	}
	return ApplyAdd(context);
}

void Engine::AddScriptEntryCommand::Undo(EditorCommandContext& context) {

	ApplyRestore(context);
}

bool Engine::AddScriptEntryCommand::Redo(EditorCommandContext& context) {

	return ApplyAdd(context);
}
