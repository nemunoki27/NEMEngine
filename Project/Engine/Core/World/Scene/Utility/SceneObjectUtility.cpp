#include "SceneObjectUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>

namespace Engine::SceneObjectUtility {

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
