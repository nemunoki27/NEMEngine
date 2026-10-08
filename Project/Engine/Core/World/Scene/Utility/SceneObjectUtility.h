#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Assets/AssetTypes.h>

namespace Engine {

	class ECSWorld;
	struct SceneObjectComponent;

	namespace SceneObjectUtility {

		// SceneObjectComponentが無ければ付与し、localFileIDが無ければ生成して返す
		SceneObjectComponent& EnsureSceneObject(ECSWorld& world, Entity entity);
		// activeSelfを変更し、親子階層のactiveInHierarchyへ反映する
		bool SetActiveSelf(ECSWorld& world, Entity entity, bool active);

		// Entityが所属するシーンインスタンスIDを取得する
		UUID GetSceneInstanceID(const ECSWorld& world, Entity entity);

		// 指定したシーンインスタンスに所属しているか
		bool IsInScene(const ECSWorld& world, Entity entity, UUID sceneInstanceID);

		// 文書内IDが一意に一致するEntityを探す
		Entity FindByLocalFileID(const ECSWorld& world, UUID localFileID);
		// 指定したシーンインスタンス内のlocalFileIDからEntityを探す
		Entity FindByLocalFileID(const ECSWorld& world, UUID sceneInstanceID, UUID localFileID);
		// AssetとLocalFileIDが一致し、一意に決まる保存参照を解決する
		Entity ResolveReference(const ECSWorld& world, AssetID sourceAsset, UUID localFileID, UUID preferredScene = {});

	} // SceneObjectUtility
} // Engine
