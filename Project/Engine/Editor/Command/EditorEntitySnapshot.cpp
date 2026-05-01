#include "EditorEntitySnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Component/Builtin/HierarchyComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Editor/Command/EditorCommandContext.h>

//============================================================================
//	EditorEntitySnapshot classMethods
//============================================================================

namespace {

	// ルートを含むサブツリーを収集するための再帰関数
	void CollectSubtreeRecursive(Engine::ECSWorld& world,
		const Engine::Entity& entity, std::vector<Engine::Entity>& outEntities) {

		// エンティティが存在するか
		if (!world.IsAlive(entity)) {
			return;
		}

		// サブツリーのルートを収集
		outEntities.emplace_back(entity);
		if (!world.HasComponent<Engine::HierarchyComponent>(entity)) {
			return;
		}

		// 子エンティティを再帰的に収集
		const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
		Engine::Entity child = hierarchy.firstChild;
		while (child.IsValid() && world.IsAlive(child)) {

			CollectSubtreeRecursive(world, child, outEntities);
			if (!world.HasComponent<Engine::HierarchyComponent>(child)) {
				break;
			}
			child = world.GetComponent<Engine::HierarchyComponent>(child).nextSibling;
		}
	}
}


void Engine::EditorEntityTreeSnapshot::Clear() {

	rootStableUUID = UUID{};
	ownerSceneInstanceID = UUID{};
	ownerSourceAsset = AssetID{};
	entities.clear();
}

std::vector<Engine::Entity> Engine::EditorEntitySnapshotUtility::CollectSubtreeEntities(ECSWorld& world, const Entity& root) {

	std::vector<Entity> entities{};
	if (!world.IsAlive(root)) {
		return entities;
	}
	// ルートを含むサブツリーを収集する
	CollectSubtreeRecursive(world, root, entities);
	return entities;
}

void Engine::EditorEntitySnapshotUtility::CaptureSubtree(ECSWorld& world,
	const Entity& root, EditorEntityTreeSnapshot& outSnapshot) {

	// スナップショットをクリアする
	outSnapshot.Clear();
	if (!world.IsAlive(root)) {
		return;
	}

	// ルートエンティティのUUIDをスナップショットに保存する
	outSnapshot.rootStableUUID = world.GetUUID(root);

	// ランタイム情報をスナップショットに保存する
	if (world.HasComponent<SceneObjectComponent>(root)) {

		const auto& sceneObject = world.GetComponent<SceneObjectComponent>(root);
		outSnapshot.ownerSceneInstanceID = sceneObject.sceneInstanceID;
		outSnapshot.ownerSourceAsset = sceneObject.sourceAsset;
	}

	// ルートを含むサブツリーを収集する
	const std::vector<Entity> entities = CollectSubtreeEntities(world, root);
	outSnapshot.entities.reserve(entities.size());
	for (const auto& entity : entities) {

		// エンティティのUUIDとコンポーネントをスナップショットに保存する
		SerializedEntitySnapshot snapshot{};
		snapshot.stableUUID = world.GetUUID(entity);
		world.SerializeEntityComponents(entity, snapshot.components);

		// スナップショットに追加する
		outSnapshot.entities.emplace_back(std::move(snapshot));
	}
}

std::vector<Engine::Entity> Engine::EditorEntitySnapshotUtility::RestoreSubtree(ECSWorld& world,
	const EditorEntityTreeSnapshot& snapshot) {

	// スナップショットが空の場合は何もしない
	std::vector<Entity> restored;
	restored.reserve(snapshot.entities.size());
	for (const auto& entitySnapshot : snapshot.entities) {

		// スナップショットからエンティティを復元する
		Entity entity = world.CreateEntity(entitySnapshot.stableUUID);

		// スナップショットからコンポーネントを復元する
		for (auto it = entitySnapshot.components.begin(); it != entitySnapshot.components.end(); ++it) {

			world.AddComponentFromJson(entity, it.key(), it.value());
		}
		restored.emplace_back(entity);
	}
	return restored;
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
