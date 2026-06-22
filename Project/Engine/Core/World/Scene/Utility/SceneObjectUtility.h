#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

namespace Engine {

	class ECSWorld;
	struct SceneObjectComponent;

	namespace SceneObjectUtility {

		// SceneObjectComponentが無ければ付与し、localFileIDが無ければ生成して返す
		SceneObjectComponent& EnsureSceneObject(ECSWorld& world, Entity entity);

		// Entityが所属するシーンインスタンスIDを取得する
		UUID GetSceneInstanceID(ECSWorld& world, Entity entity);

		// 指定したシーンインスタンスに所属しているか
		bool IsInScene(ECSWorld& world, Entity entity, UUID sceneInstanceID);

		// localFileIDからEntityを探す、Edit/Playをまたいで安定するエンティティ参照の解決に使う
		Entity FindByLocalFileID(ECSWorld& world, UUID localFileID);

	} // SceneObjectUtility
} // Engine
