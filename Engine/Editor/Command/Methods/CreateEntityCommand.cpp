#include "CreateEntityCommand.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Editor/EditorState.h>
#include <Engine/Editor/Command/EditorEntitySnapshot.h>
#include <Engine/Core/Scene/SceneAuthoring.h>

//============================================================================
//	CreateEntityCommand classMethods
//============================================================================

namespace {

	// UUIDからエンティティを検索する。UUIDが無効な場合はNullエンティティを返す
	Engine::Entity FindEntityByUUID(Engine::ECSWorld& world, Engine::UUID id) {

		if (!id) {
			return Engine::Entity::Null();
		}
		return world.FindByUUID(id);
	}
	// エンティティの所属先を解決する
	void ResolveOwnerRuntimeState(const Engine::EditorCommandContext& context,
		Engine::ECSWorld& world, const Engine::Entity& parent,
		Engine::UUID& outSceneInstanceID, Engine::AssetID& outSourceAsset) {

		outSceneInstanceID = Engine::UUID{};
		outSourceAsset = Engine::AssetID{};

		// 親がある場合は親の所属先を最優先で使う
		if (parent.IsValid() && world.IsAlive(parent) &&
			world.HasComponent<Engine::SceneObjectComponent>(parent)) {

			const auto& parentSceneObject = world.GetComponent<Engine::SceneObjectComponent>(parent);
			outSceneInstanceID = parentSceneObject.sceneInstanceID;
			outSourceAsset = parentSceneObject.sourceAsset;
		}

		// 親から取れなければアクティブシーンを使う
		if (context.editorContext) {

			if (!outSceneInstanceID) {
				outSceneInstanceID = context.editorContext->activeSceneInstanceID;
			}
			if (!outSourceAsset) {
				outSourceAsset = context.editorContext->activeSceneAsset;
			}
		}
	}
}

Engine::CreateEntityCommand::CreateEntityCommand(const std::string& name, UUID parentStableUUID) :
	name_(name), parentStableUUID_(parentStableUUID) {
}

bool Engine::CreateEntityCommand::CreateInternal(EditorCommandContext& context) {

	// 編集可能か
	if (!context.CanEditScene()) {
		return false;
	}

	ECSWorld* world = context.GetWorld();
	if (!world) {
		return false;
	}

	// Redoでも同じUUIDを使えるようにする
	Entity entity = world->CreateEntity(createdStableUUID_);
	if (!createdStableUUID_) {

		createdStableUUID_ = world->GetUUID(entity);
	}

	// デフォルトのコンポーネントを追加する
	SceneAuthoring::EnsureGameObjectDefaults(*world, entity, name_);

	// 親指定がある場合は親子付け
	Entity parent = FindEntityByUUID(*world, parentStableUUID_);

	// 新規作成時点でシーン所属を決める
	UUID resolvedSceneInstanceID{};
	AssetID resolvedSourceAsset{};
	ResolveOwnerRuntimeState(context, *world, parent, resolvedSceneInstanceID, resolvedSourceAsset);

	// シーン所属の情報をコンポーネントに書き込む
	if (world->HasComponent<SceneObjectComponent>(entity)) {

		auto& sceneObject = world->GetComponent<SceneObjectComponent>(entity);

		if (resolvedSceneInstanceID) {
			sceneObject.sceneInstanceID = resolvedSceneInstanceID;
		}
		if (resolvedSourceAsset) {
			sceneObject.sourceAsset = resolvedSourceAsset;
		}
	}

	// 親指定がある場合は親子付け
	if (parent.IsValid() && world->IsAlive(parent)) {

		HierarchySystem hierarchySystem;
		hierarchySystem.SetParent(*world, entity, parent);
	}

	if (context.editorState) {

		context.editorState->SelectEntity(entity);
	}
	return true;
}

bool Engine::CreateEntityCommand::Execute(EditorCommandContext& context) {

	return CreateInternal(context);
}

void Engine::CreateEntityCommand::Undo(EditorCommandContext& context) {

	ECSWorld* world = context.GetWorld();
	if (!world || !createdStableUUID_) {
		return;
	}

	// UUIDからエンティティを検索し、存在する場合は破棄する
	Entity created = world->FindByUUID(createdStableUUID_);
	if (world->IsAlive(created)) {

		EditorEntitySnapshotUtility::DestroySubtree(*world, created);
		context.RebuildHierarchyAll();
	}
	// 親エンティティが存在する場合は選択を更新する
	if (context.editorState) {

		Entity parent = world->FindByUUID(parentStableUUID_);
		context.editorState->SelectEntity((world->IsAlive(parent) ? parent : Entity::Null()));
	}
}

bool Engine::CreateEntityCommand::Redo(EditorCommandContext& context) {

	return CreateInternal(context);
}