#include "InstantiatePrefabCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Editor/EditorState.h>
#include <Engine/Editor/Command/EditorEntitySnapshot.h>
#include <Engine/Core/Prefab/PrefabSystem.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>

//============================================================================
//	InstantiatePrefabCommand classMethods
//============================================================================

Engine::InstantiatePrefabCommand::InstantiatePrefabCommand(AssetID prefabAsset, UUID parentStableUUID) :
	prefabAsset_(prefabAsset), parentStableUUID_(parentStableUUID) {
}

bool Engine::InstantiatePrefabCommand::InstantiateInternal(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene() || !prefabAsset_) {
		return false;
	}
	if (!context.editorContext || !context.editorContext->assetDatabase) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// 親指定がある場合はUUIDから現在のEntityを引き直す
	Entity parent = Entity::Null();
	if (parentStableUUID_) {

		parent = world->FindByUUID(parentStableUUID_);
		if (!world->IsAlive(parent)) {
			parent = Entity::Null();
		}
	}

	// PrefabSystemに渡す生成オプション
	PrefabInstantiateDesc desc{};
	desc.ownerSceneInstanceID = context.editorContext->activeSceneInstanceID;
	desc.parent = parent;

	HierarchySystem hierarchySystem{};
	PrefabSystem prefabSystem{};
	PrefabInstantiateResult result{};
	if (!prefabSystem.InstantiatePrefab(
		*context.editorContext->assetDatabase,
		hierarchySystem,
		*world,
		prefabAsset_,
		result,
		desc)) {
		return false;
	}

	if (!world->IsAlive(result.root)) {
		return false;
	}

	instantiatedRootStableUUID_ = world->GetUUID(result.root);
	context.RebuildHierarchyAll();

	// 生成後はPrefabルートを選択する
	if (context.editorState) {
		context.editorState->SelectEntity(result.root);
	}
	return true;
}

bool Engine::InstantiatePrefabCommand::Execute(EditorCommandContext& context) {

	return InstantiateInternal(context);
}

void Engine::InstantiatePrefabCommand::Undo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !instantiatedRootStableUUID_) {
		return;
	}

	// 直前に生成したPrefabインスタンスをルートごと削除する
	Entity root = world->FindByUUID(instantiatedRootStableUUID_);
	if (world->IsAlive(root)) {

		EditorEntitySnapshotUtility::DestroySubtree(*world, root);
		context.RebuildHierarchyAll();
	}

	// Undo後は親があれば親を選択、なければ選択解除
	if (context.editorState) {

		Entity parent = world->FindByUUID(parentStableUUID_);
		context.editorState->SelectEntity(world->IsAlive(parent) ? parent : Entity::Null());
	}
}

bool Engine::InstantiatePrefabCommand::Redo(EditorCommandContext& context) {

	return InstantiateInternal(context);
}
