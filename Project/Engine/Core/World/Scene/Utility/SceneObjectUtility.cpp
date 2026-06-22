#include "SceneObjectUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

namespace Engine::SceneObjectUtility {

	SceneObjectComponent& EnsureSceneObject(ECSWorld& world, Entity entity) {

		// シーンオブジェクトが存在しないなら付ける
		if (!world.HasComponent<SceneObjectComponent>(entity)) {

			auto& sceneObject = world.AddComponent<SceneObjectComponent>(entity);
			sceneObject.localFileID = UUID::New();
			sceneObject.activeSelf = true;
			sceneObject.activeInHierarchy = true;
		}

		// ローカルIDが存在しないなら新しく生成する
		auto& sceneObject = world.GetComponent<SceneObjectComponent>(entity);
		if (!sceneObject.localFileID) {
			sceneObject.localFileID = UUID::New();
		}
		return sceneObject;
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

		if (!localFileID) {
			return Entity::Null();
		}
		Entity found = Entity::Null();
		world.ForEach<SceneObjectComponent>([&](Entity entity, SceneObjectComponent& sceneObject) {
			if (!found.IsValid() && sceneObject.localFileID == localFileID) {
				found = entity;
			}
			});
		return found;
	}
} // Engine::SceneObjectUtility
