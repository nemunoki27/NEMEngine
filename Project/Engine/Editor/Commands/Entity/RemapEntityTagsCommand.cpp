#include "RemapEntityTagsCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

//============================================================================
//	RemapEntityTagsCommand classMethods
//============================================================================
Engine::RemapEntityTagsCommand::RemapEntityTagsCommand(const std::string& fromTag, const std::string& toTag) :
	fromTag_(fromTag),
	toTag_(toTag) {
}

bool Engine::RemapEntityTagsCommand::ApplyTag(EditorCommandContext& context, const std::string& tag) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 保存済みUUIDを辿り、生存している対象だけタグを書き換える
	for (const UUID& uuid : affected_) {

		const Entity target = world->FindByUUID(uuid);
		if (!world->IsAlive(target)) {
			continue;
		}
		if (SceneObjectComponent* sceneObject = world->TryGetComponent<SceneObjectComponent>(target)) {
			sceneObject->tag = tag;
		}
	}
	return true;
}

bool Engine::RemapEntityTagsCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 同名や空への付け替えは何も変えない
	if (fromTag_ == toTag_ || fromTag_.empty()) {
		return false;
	}

	// 初回実行なら、付け替え対象のUUIDを集める
	if (!captured_) {

		world->ForEach<SceneObjectComponent>([&](Entity entity, SceneObjectComponent& sceneObject) {
			if (sceneObject.tag == fromTag_) {
				affected_.push_back(world->GetUUID(entity));
			}
			});
		captured_ = true;

		// 対象が無ければコマンド履歴へ積まない
		if (affected_.empty()) {
			return false;
		}
	}
	return ApplyTag(context, toTag_);
}

void Engine::RemapEntityTagsCommand::Undo(EditorCommandContext& context) {

	ApplyTag(context, fromTag_);
}

bool Engine::RemapEntityTagsCommand::Redo(EditorCommandContext& context) {

	return ApplyTag(context, toTag_);
}
