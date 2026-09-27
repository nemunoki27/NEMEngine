#include "EntityTreeSnapshot.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Serialization/SceneCreationScope.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchyUtility.h>

// c++
#include <stdexcept>

//============================================================================
//	EntityTreeSnapshot structMethods
//============================================================================
void Engine::EntityTreeSnapshot::Clear() {

	rootStableUUID = UUID{};
	ownerSceneInstanceID = UUID{};
	ownerSourceAsset = AssetID{};
	entities.clear();
}

void Engine::EntitySnapshotUtility::CaptureSubtree(ECSWorld& world,
	const Entity& root, EntityTreeSnapshot& outSnapshot) {

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
	const std::vector<Entity> entities = HierarchyUtility::CollectLogicalSubtree(world, root);
	outSnapshot.entities.reserve(entities.size());
	for (const auto& entity : entities) {

		// エンティティのUUIDとコンポーネントをスナップショットに保存する
		SerializedEntitySnapshot snapshot{};
		snapshot.stableUUID = world.GetUUID(entity);
		if (const auto* membership = world.TryGetComponent<SceneObjectComponent>(entity)) {
			snapshot.sceneInstanceID = membership->sceneInstanceID;
			snapshot.sourceAsset = membership->sourceAsset;
		}
		world.SerializeEntityComponents(entity, snapshot.components);

		// スナップショットに追加する
		outSnapshot.entities.emplace_back(std::move(snapshot));
	}
	CaptureReferenceTargets(world, outSnapshot);
}

std::vector<Engine::Entity> Engine::EntitySnapshotUtility::RestoreSubtree(ECSWorld& world,
	const EntityTreeSnapshot& snapshot) {

	// スナップショットが空の場合は何もしない
	if (snapshot.IsEmpty()) {
		return {};
	}
	SceneCreationScope creation(world);
	std::vector<Entity> restored;
	restored.reserve(snapshot.entities.size());
	for (const auto& entitySnapshot : snapshot.entities) {

		// スナップショットからエンティティを復元する
		Entity entity = world.CreateEntity(entitySnapshot.stableUUID);

		// スナップショットからコンポーネントを復元する
		for (auto it = entitySnapshot.components.begin(); it != entitySnapshot.components.end(); ++it) {

			if (!world.AddComponentFromJson(entity, it.key(), it.value())) {
				throw std::runtime_error("Componentの復元に失敗しました");
			}
		}
		// JSONに含めないScene所属と参照元Assetを復元
		if (auto* membership = world.TryGetComponent<SceneObjectComponent>(entity)) {
			membership->sceneInstanceID = entitySnapshot.sceneInstanceID ?
				entitySnapshot.sceneInstanceID : snapshot.ownerSceneInstanceID;
			membership->sourceAsset = entitySnapshot.sourceAsset ?
				entitySnapshot.sourceAsset : snapshot.ownerSourceAsset;
			world.MarkComponentModified<SceneObjectComponent>(entity);
		}
		restored.emplace_back(entity);
	}
	creation.Commit();
	return restored;
}
