#include "SetEntityActiveCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>

//============================================================================
//	SetEntityActiveCommand classMethods
//============================================================================

Engine::SetEntityActiveCommand::SetEntityActiveCommand(const Entity& targetEntity, bool activeSelf) :
	initialTarget_(targetEntity),
	afterActiveSelf_(activeSelf) {
}

bool Engine::SetEntityActiveCommand::Apply(EditorCommandContext& context, bool activeSelf) {

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

	// シーンオブジェクトコンポーネントがなければ付ける
	if (!world->HasComponent<SceneObjectComponent>(target)) {

		auto& sceneObject = world->AddComponent<SceneObjectComponent>(target);
		sceneObject.localFileID = UUID::New();
		sceneObject.activeSelf = true;
		sceneObject.activeInHierarchy = true;
	}

	// アクティブ状態を変更する
	auto& sceneObject = world->GetComponent<SceneObjectComponent>(target);
	sceneObject.activeSelf = activeSelf;

	// 階層内のアクティブをルート以下で再計算する
	HierarchySystem hierarchySystem;
	hierarchySystem.RefreshActiveTree(*world, target);
	if (context.editorState) {

		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::SetEntityActiveCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行なら、対象エンティティのUUIDを取得して、元のアクティブ状態を保存する
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