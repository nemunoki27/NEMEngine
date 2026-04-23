#include "AddScriptEntryCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/ScriptComponent.h>
#include <Engine/Editor/EditorState.h>

//============================================================================
//	AddScriptEntryCommand classMethods
//============================================================================

namespace {

	// ScriptEntryを生成する
	Engine::ScriptEntry MakeScriptEntry(const std::string& typeName, Engine::AssetID scriptAsset) {

		Engine::ScriptEntry entry{};
		entry.type = typeName;
		entry.scriptAsset = scriptAsset;
		entry.enabled = true;
		entry.serializedFields = nlohmann::json::object();
		entry.handle = Engine::BehaviorHandle::Null();
		return entry;
	}
}

Engine::AddScriptEntryCommand::AddScriptEntryCommand(const Entity& targetEntity,
	const std::string_view& typeName, AssetID scriptAsset) :
	initialTarget_(targetEntity),
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

	// UUIDからエンティティを検索する
	Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	// ScriptComponentが無い場合は追加してからScriptEntryを積む
	if (!world->HasComponent<ScriptComponent>(target)) {
		if (!world->AddComponentByName(target, "Script")) {
			return false;
		}
	}

	auto& component = world->GetComponent<ScriptComponent>(target);
	component.scripts.emplace_back(MakeScriptEntry(typeName_, scriptAsset_));

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

		world->RemoveComponentByName(target, "Script");
	} else {

		world->AddComponentFromJson(target, "Script", beforeData_);
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
			if (!world->SerializeComponentToJson(initialTarget_, "Script", beforeData_)) {
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
