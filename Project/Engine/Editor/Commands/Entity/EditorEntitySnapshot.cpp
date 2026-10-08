#include "EditorEntitySnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Components/Rendering/MeshRendererComponent.h>
#include <Engine/Core/Rendering/Meshes/MeshSubMeshAuthoring.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>
#include <Engine/Editor/Commands/Core/EditorCommandContext.h>
#include <Engine/Editor/Core/EditorState.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>

// c++
#include <stdexcept>

//============================================================================
//	EditorEntitySnapshot classMethods
//============================================================================
std::vector<Engine::Entity> Engine::EditorEntitySnapshotUtility::CollectSubtreeEntities(ECSWorld& world, const Entity& root) {

	return HierarchyUtility::CollectLogicalSubtree(world, root);
}

void Engine::EditorEntitySnapshotUtility::CaptureSubtree(ECSWorld& world,
	const Entity& root, EditorEntityTreeSnapshot& outSnapshot) {

	EntitySnapshotUtility::CaptureSubtree(world, root, outSnapshot);
}

std::vector<Engine::Entity> Engine::EditorEntitySnapshotUtility::RestoreSubtree(ECSWorld& world,
	const EditorEntityTreeSnapshot& snapshot) {

	return EntitySnapshotUtility::RestoreSubtree(world, snapshot);
}

Engine::Entity Engine::EditorEntitySnapshotUtility::RestoreCommandSnapshot(EditorCommandContext& context,
	const EditorEntityTreeSnapshot& snapshot) {

	if (!context.CanEditScene() || snapshot.IsEmpty()) {
		return Entity::Null();
	}
	ECSWorld& world = *context.GetWorld();
	SceneCreationScope creation(world);
	const std::vector<Entity> restored = RestoreSubtree(world, snapshot);
	RefreshRestoredRuntimeState(context, world, snapshot, restored);
	context.RebuildHierarchyAll();
	const Entity root = world.FindByUUID(snapshot.rootStableUUID);
	if (!world.IsAlive(root)) {
		throw std::runtime_error("復元した階層にルートがありません");
	}
	if (context.editorState) {
		context.editorState->SelectEntity(root);
	}
	// 後処理まで成功してから生成物を公開
	creation.Commit();
	return root;
}

void Engine::EditorEntitySnapshotUtility::RefreshRestoredRuntimeState(const EditorCommandContext& context,
	ECSWorld& world, const EditorEntityTreeSnapshot& snapshot, std::span<const Entity> restoredEntities) {

	UUID sceneInstanceID = snapshot.ownerSceneInstanceID;
	AssetID sourceAsset = snapshot.ownerSourceAsset;

	// Delete/Undoなどで復元したEntityは、SceneObjectのランタイム所属情報がJSONから戻らない
	if (context.editorContext) {

		if (!sceneInstanceID) {
			sceneInstanceID = context.editorContext->activeSceneInstanceID;
		}
		if (!sourceAsset) {
			sourceAsset = context.editorContext->activeSceneAsset;
		}
	}

	AssetDatabase* assetDatabase = context.editorContext ? context.editorContext->assetDatabase : nullptr;
	for (const Entity& entity : restoredEntities) {

		if (!world.IsAlive(entity)) {
			continue;
		}

		if (world.HasComponent<SceneObjectComponent>(entity)) {

			auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
			if (!sceneObject.sceneInstanceID && sceneInstanceID) {
				sceneObject.sceneInstanceID = sceneInstanceID;
			}
			if (!sceneObject.sourceAsset && sourceAsset) {
				sceneObject.sourceAsset = sourceAsset;
			}
		}

		if (world.HasComponent<MeshRendererComponent>(entity)) {

			// Sceneロード時と同じく、meshアセットからsubMesh設定を補完する
			MeshSubMeshAuthoring::SyncEntity(assetDatabase, world, entity, true);
		}
	}
}

void Engine::EditorEntitySnapshotUtility::DestroySubtree(ECSWorld& world, const Entity& root) {

	// ルートを含むサブツリーを収集する
	const std::vector<Entity> entities = CollectSubtreeEntities(world, root);

	// 子から順に破棄する
	for (auto it = entities.rbegin(); it != entities.rend(); ++it) {
		if (world.IsAlive(*it)) {

			world.DestroyEntity(*it);
		}
	}

	// エディタコマンドは直後にHierarchy再構築やUndo/Redoを行うため、ここで状態を確定する
	world.FlushPendingDestroyEntities();
}

void Engine::EditorEntitySnapshotUtility::FillMissingOwnerRuntimeState(const EditorCommandContext& context,
	ECSWorld& world, UUID parentStableUUID, EditorEntityTreeSnapshot& snapshot) {

	if (snapshot.IsEmpty()) {
		return;
	}

	Engine::UUID resolvedSceneInstanceID = snapshot.ownerSceneInstanceID;
	Engine::AssetID resolvedSourceAsset = snapshot.ownerSourceAsset;

	// 外部親があれば、その親のシーン所属で補完する
	if ((!resolvedSceneInstanceID || !resolvedSourceAsset) && parentStableUUID) {

		Engine::Entity parent = world.FindByUUID(parentStableUUID);
		if (world.IsAlive(parent) && world.HasComponent<Engine::SceneObjectComponent>(parent)) {

			const auto& parentSceneObject = world.GetComponent<Engine::SceneObjectComponent>(parent);
			if (!resolvedSceneInstanceID) {
				resolvedSceneInstanceID = parentSceneObject.sceneInstanceID;
			}
			if (!resolvedSourceAsset) {
				resolvedSourceAsset = parentSceneObject.sourceAsset;
			}
		}
	}

	// それでも足りなければアクティブシーンで補完する
	if (context.editorContext) {

		if (!resolvedSceneInstanceID) {
			resolvedSceneInstanceID = context.editorContext->activeSceneInstanceID;
		}
		if (!resolvedSourceAsset) {
			resolvedSourceAsset = context.editorContext->activeSceneAsset;
		}
	}
	snapshot.ownerSceneInstanceID = resolvedSceneInstanceID;
	snapshot.ownerSourceAsset = resolvedSourceAsset;
}
