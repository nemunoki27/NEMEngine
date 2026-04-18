#include "SetSerializedComponentCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/Render/MeshRendererComponent.h>

//============================================================================
//	SetSerializedComponentCommand classMethods
//============================================================================

Engine::SetSerializedComponentCommand::SetSerializedComponentCommand(const Entity& targetEntity,
	const std::string_view& typeName, const nlohmann::json& beforeData, const nlohmann::json& afterData) :
	initialTarget_(targetEntity), typeName_(typeName), beforeData_(beforeData), afterData_(afterData) {}

bool Engine::SetSerializedComponentCommand::Apply(EditorCommandContext& context, const nlohmann::json& data) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world || !targetStableUUID_) {
		return false;
	}

	// UUIDからエンティティを検索
	Entity target = world->FindByUUID(targetStableUUID_);
	if (!world->IsAlive(target)) {
		return false;
	}

	world->AddComponentFromJson(target, typeName_, data);
	if (typeName_ == "MeshRenderer" && world->HasComponent<MeshRendererComponent>(target)) {

		auto& meshRenderer = world->GetComponent<MeshRendererComponent>(target);

		Matrix4x4 parentWorld = Matrix4x4::Identity();
		if (world->HasComponent<TransformComponent>(target)) {
			parentWorld = world->GetComponent<TransformComponent>(target).worldMatrix;
		}
		MeshSubMeshRuntime::UpdateRendererRuntime(meshRenderer, parentWorld);
	}

	if (context.editorState) {
		if (context.editorState->selectedEntity == target) {

			context.editorState->ValidateSelection(world);
		} else {

			context.editorState->SelectEntity(target);
		}
	}
	return true;
}

bool Engine::SetSerializedComponentCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行時はエンティティのUUIDを取得して保存する
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}
		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (beforeData_ == afterData_) {
			return false;
		}
	}
	return Apply(context, afterData_);
}

void Engine::SetSerializedComponentCommand::Undo(EditorCommandContext& context) {

	Apply(context, beforeData_);
}

bool Engine::SetSerializedComponentCommand::Redo(EditorCommandContext& context) {

	return Apply(context, afterData_);
}