#include "SceneObjectUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>

namespace Engine::SceneObjectUtility {

	Entity ResolveReference(ECSWorld& world, AssetID sourceAsset, UUID localFileID, UUID preferredScene) {

		if (!sourceAsset || !localFileID) {
			return Entity::Null();
		}
		Entity match = Entity::Null();
		Entity preferred = Entity::Null();
		uint32_t matches = 0;
		uint32_t preferredMatches = 0;
		world.ForEach<SceneObjectComponent>([&](Entity entity, const SceneObjectComponent& object) {
			if (world.IsPendingDestroy(entity) || object.sourceAsset != sourceAsset || object.localFileID != localFileID) {
				return;
			}
			match = entity;
			++matches;
			if (preferredScene && object.sceneInstanceID == preferredScene) {
				preferred = entity;
				++preferredMatches;
			}
		});
		// 所有Scene内でも複数一致した参照は未解決のまま返す
		if (preferredMatches != 0) {
			return preferredMatches == 1 ? preferred : Entity::Null();
		}
		return matches == 1 ? match : Entity::Null();
	}

	SceneObjectComponent& EnsureSceneObject(ECSWorld& world, Entity entity) {

		// シーンオブジェクトが存在しないなら付ける
		if (!world.HasComponent<SceneObjectComponent>(entity)) {

			world.AddComponent<SceneObjectComponent>(entity);
		}

		// ローカルIDが存在しないなら新しく生成する
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		if (!sceneObject.localFileID) {
			sceneObject.localFileID = UUID::New();
		}
		return sceneObject;
	}

	bool SetActiveSelf(ECSWorld& world, Entity entity, bool active) {

		if (!world.IsAlive(entity)) {
			return false;
		}

		const bool hadSceneObject = world.HasComponent<SceneObjectComponent>(entity);
		SceneObjectComponent& sceneObject = EnsureSceneObject(world, entity);
		if (hadSceneObject && sceneObject.activeSelf == active) {
			return false;
		}

		const bool previousActiveInHierarchy = sceneObject.activeInHierarchy;
		sceneObject.activeSelf = active;

		HierarchySystem hierarchySystem{};
		hierarchySystem.UpdateActiveInHierarchy(world, entity);

		// 親が非アクティブでactiveInHierarchyが変化しない場合もactiveSelfの変更を通知する
		const SceneObjectComponent* updatedSceneObject =
			world.TryGetComponent<SceneObjectComponent>(entity);
		if (updatedSceneObject &&
			updatedSceneObject->activeInHierarchy == previousActiveInHierarchy) {
			world.MarkComponentModified<SceneObjectComponent>(entity);
		}
		return true;
	}

	UUID GetSceneInstanceID(ECSWorld& world, Entity entity) {

		if (const auto* component = world.TryGetComponent<SceneObjectComponent>(entity)) {
			return component->sceneInstanceID;
		}
		return {};
	}

	bool IsInScene(ECSWorld& world, Entity entity, UUID sceneInstanceID) {

		if (!sceneInstanceID) {
			return true;
		}
		return GetSceneInstanceID(world, entity) == sceneInstanceID;
	}

	Entity FindByLocalFileID(ECSWorld& world, UUID localFileID) {

		return FindByLocalFileID(world, UUID{}, localFileID);
	}

	Entity FindByLocalFileID(ECSWorld& world, UUID sceneInstanceID, UUID localFileID) {

		if (!localFileID) {
			return Entity::Null();
		}
		Entity found = Entity::Null();
		uint32_t matches = 0;
		world.ForEach<SceneObjectComponent>([&](Entity entity, const SceneObjectComponent& sceneObject) {
			if (!world.IsPendingDestroy(entity) && sceneObject.localFileID == localFileID &&
				(!sceneInstanceID || sceneObject.sceneInstanceID == sceneInstanceID)) {
				found = entity;
				++matches;
			}
			});
		// 重複する番号を走査順で選ばない
		return matches == 1 ? found : Entity::Null();
	}
} // Engine::SceneObjectUtility
