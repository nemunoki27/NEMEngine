#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	class ECSWorld;

	namespace SceneObjectUtility {

		// Entityが所属するシーンインスタンスIDを取得する
		UUID GetSceneInstanceID(ECSWorld& world, Entity entity);

		// 指定したシーンインスタンスに所属しているか
		bool IsInScene(ECSWorld& world, Entity entity, UUID sceneInstanceID);

	} // SceneObjectUtility
} // Engine
