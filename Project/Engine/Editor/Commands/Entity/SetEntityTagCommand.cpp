#include "SetEntityTagCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

//============================================================================
//	SetEntityTagCommand classMethods
//============================================================================
Engine::SetEntityTagCommand::SetEntityTagCommand(const Entity& targetEntity, const std::string& tag) :
	initialTarget_(targetEntity),
	afterTag_(tag) {
}

bool Engine::SetEntityTagCommand::Apply(EditorCommandContext& context, const std::string& tag) {

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

	// シーンオブジェクトコンポーネントがなければ付ける、アクティブ変更コマンドと同じ前提
	if (!world->HasComponent<SceneObjectComponent>(target)) {

		auto& sceneObject = world->AddComponent<SceneObjectComponent>(target);
		sceneObject.localFileID = UUID::New();
	}

	// タグを変更する
	world->GetComponent<SceneObjectComponent>(target).tag = tag;
	if (context.editorState) {

		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetEntityTagCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行なら、対象エンティティのUUIDを取得して、元のタグを保存する
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
