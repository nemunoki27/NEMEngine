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
			return true; // インスタンスID未指定なら全シーン対象とみなす（既存互換）
		}
		return GetSceneInstanceID(world, entity) == sceneInstanceID;
	}

} // Engine::SceneObjectUtility
