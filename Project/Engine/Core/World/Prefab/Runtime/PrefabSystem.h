#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiation.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideTypes.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabHeader.h>

// c++
#include <filesystem>
#include <cstdint>
#include <vector>
#include <utility>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	class HierarchySystem;

	//============================================================================
	//	PrefabSystem class
	//	プレファブの保存と生成を行うシステム
	//============================================================================
	class PrefabSystem {
	public:
		//============================================================================
		//	public Methods
		//============================================================================

		PrefabSystem() = default;
		~PrefabSystem() = default;

		// プレファブ保存、rootサブツリーを保存する
		bool SavePrefab(AssetDatabase& database, ECSWorld& world,
			const Entity& root, const std::string& prefabAssetPath,
			UUID prefabInstanceID = UUID{}) const;
		// 明示したエンティティ集合を保存する、複数ルートのプレファブ編集で使う
		// headerのrootLocalFileIDにはrootのlocalFileIDを使う
		bool SavePrefabFromEntities(AssetDatabase& database, ECSWorld& world, const Entity& root,
			const std::vector<Entity>& entities, const std::string& prefabAssetPath,
			UUID prefabInstanceID = UUID{}) const;

		// EntityへPrefabLinkを設定する
		void SetPrefabLink(ECSWorld& world, const Entity& entity, AssetID prefabAsset,
			UUID prefabLocalFileID, UUID prefabInstanceID, bool isPrefabRoot,
			UUID ownerPrefabInstanceID = UUID{}, UUID nestedSlotID = UUID{},
			bool isPrefabAssetNested = false) const;
		// サブツリーを1つのプレファブインスタンスとして設定し、使用したインスタンスIDを返す
		UUID SetPrefabLinkToSubtree(ECSWorld& world, const Entity& root, AssetID prefabAsset,
			UUID prefabInstanceID = UUID{}) const;
		// Prefabインスタンスのリンクを解除する
		bool UnpackPrefabInstance(ECSWorld& world, const Entity& root, PrefabUnpackMode mode) const;

		// プレファブ生成
		bool InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem, ECSWorld& world,
			AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc = {}) const;
		bool InstantiatePrefabFromPath(AssetDatabase& database, HierarchySystem& hierarchySystem, ECSWorld& world,
			const std::string& prefabAssetPath, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc = {}) const;
	};
} // Engine
