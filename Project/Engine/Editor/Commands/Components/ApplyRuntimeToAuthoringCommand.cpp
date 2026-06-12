#include "ApplyRuntimeToAuthoringCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorContext.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

//============================================================================
//	ApplyRuntimeToAuthoringCommand classMethods
//============================================================================
Engine::ApplyRuntimeToAuthoringCommand::ApplyRuntimeToAuthoringCommand(const UUID& entityStableUUID,
	const std::string_view& typeName, const nlohmann::json& beforeData, const nlohmann::json& afterData) :
	targetStableUUID_(entityStableUUID), typeName_(typeName), beforeData_(beforeData), afterData_(afterData) {}

bool Engine::ApplyRuntimeToAuthoringCommand::Apply(EditorCommandContext& context, const nlohmann::json& data) {

	// 対象はEditWorldのauthoringでPlay中でも有効、CanEditSceneは広げない
	ECSWorld* editWorld = context.editorContext ? context.editorContext->editWorld : nullptr;
	if (!editWorld || !targetStableUUID_) {
		return false;
	}
	// runtime entityではなくEditWorldの対応entityをstable UUIDで解決する
	const Entity target = editWorld->FindByUUID(targetStableUUID_);
	if (!editWorld->IsAlive(target)) {
		return false;
	}
	editWorld->AddComponentFromJson(target, typeName_, data);
	return true;
}

bool Engine::ApplyRuntimeToAuthoringCommand::Execute(EditorCommandContext& context) {

	if (beforeData_ == afterData_) {
		return false;
	}
	return Apply(context, afterData_);
}

void Engine::ApplyRuntimeToAuthoringCommand::Undo(EditorCommandContext& context) {

	Apply(context, beforeData_);
}

bool Engine::ApplyRuntimeToAuthoringCommand::Redo(EditorCommandContext& context) {

	return Apply(context, afterData_);
}
