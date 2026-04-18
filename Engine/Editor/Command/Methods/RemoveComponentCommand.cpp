#include "RemoveComponentCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>

//============================================================================
//	RemoveComponentCommand classMethods
//============================================================================

namespace {

	// 削除できないコンポーネントの種類か
	bool IsProtectedComponentType(std::string_view typeName) {

		return typeName == "Name" ||
			typeName == "Transform" ||
			typeName == "Hierarchy" ||
			typeName == "SceneObject" ||
			typeName == "PrefabLink";
	}
}

Engine::RemoveComponentCommand::RemoveComponentCommand(const Entity& targetEntity, const std::string_view& typeName) :
	initialTarget_(targetEntity),
	typeName_(typeName) {}

bool Engine::RemoveComponentCommand::ApplyRemove(EditorCommandContext& context) {

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

	// コンポーネントがないなら削除できない
	if (!world->RemoveComponentByName(target, typeName_)) {
		return false;
	}
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::RemoveComponentCommand::ApplyRestore(EditorCommandContext& context) {

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

	// コンポーネントをjsonから復元して追加する
	world->AddComponentFromJson(target, typeName_, serializedComponent_);
	if (context.editorState) {
		context.editorState->SelectEntity(target);
	}
	return true;
}

bool Engine::RemoveComponentCommand::Execute(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	// 絶対に持っていないといけないコンポーネントは削除できない
	if (IsProtectedComponentType(typeName_)) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 初回実行時は、削除するコンポーネントのデータを保存しておく
	if (!targetStableUUID_) {

		if (!world->IsAlive(initialTarget_)) {
			return false;
		}

		targetStableUUID_ = world->GetUUID(initialTarget_);
		if (!world->SerializeComponentToJson(initialTarget_, typeName_, serializedComponent_)) {
			return false;
		}
	}
	return ApplyRemove(context);
}

void Engine::RemoveComponentCommand::Undo(EditorCommandContext& context) {

	ApplyRestore(context);
}

bool Engine::RemoveComponentCommand::Redo(EditorCommandContext& context) {

	return ApplyRemove(context);
}