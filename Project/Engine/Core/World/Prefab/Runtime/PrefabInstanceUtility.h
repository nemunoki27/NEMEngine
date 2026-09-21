#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiation.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabReferenceRemapper.h>

namespace Engine {
	class ECSWorld;
}

namespace Engine::PrefabInstanceUtility {

	// SceneObjectの同一性情報を差分比較から除く
	void NormalizeSceneObjectForDiff(nlohmann::json& components);

	// SceneObjectの実行時所属を復元する
	void RestoreSceneObjectRuntimeFields(ECSWorld& world, const Entity& entity,
		AssetID sourceAsset, UUID sceneInstanceID, UUID localFileID);

	// エンティティのSceneローカルIDを取得する
	UUID SceneLocalOf(ECSWorld& world, const Entity& entity);

	// 実行時の親エンティティを取得する
	Entity ParentOf(ECSWorld& world, const Entity& entity);

	// 所属SceneとローカルIDから実体を探す
	Entity FindBySceneLocal(ECSWorld& world, UUID sceneInstanceID, UUID localFileID);

	// Prefab由来でない追加実体を収集する
	void CollectAddedSubtree(ECSWorld& world, const Entity& entity, std::vector<PrefabAddedEntity>& out);

	// SceneからPrefabへのローカルID対応を構築する
	PrefabReferenceRemapper::LocalFileIDMap BuildSceneToPrefabLocalMap(
		ECSWorld& world, const std::vector<Entity>& instanceEntities);

	// 生成結果からSceneローカルIDへの対応を構築する
	PrefabReferenceRemapper::LocalFileIDMap BuildPrefabToSceneLocalMap(
		ECSWorld& world, const PrefabInstantiateResult& result);
}
