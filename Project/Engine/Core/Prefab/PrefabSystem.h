#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Asset/AssetDatabase.h>
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>
#include <Engine/Core/Prefab/Header/PrefabHeader.h>

// c++
#include <stack>
#include <filesystem>

namespace Engine {

	//============================================================================
	//	PrefabSystem structures
	//============================================================================

	// プレファブ生成のオプション
	struct PrefabInstantiateDesc {

		// 生成先シーンインスタンスID
		UUID ownerSceneInstanceID{};

		// 生成したルートをぶら下げたい親
		Entity parent = Entity::Null();
	};
	// プレファブ生成の結果
	struct PrefabInstantiateResult {

		// 生成されたプレファブインスタンスのID
		UUID prefabInstanceID{};
		// 生成されたプレファブインスタンスのルートエンティティ
		Entity root = Entity::Null();

		// 生成されたエンティティ
		std::vector<Entity> createdEntities;

		// ローカルファイルIDから生成されたエンティティへのマップ
		std::unordered_map<UUID, Entity> sourceLocalToEntity;
	};

	//============================================================================
	//	PrefabSystem class
	//	プレファブの保存と生成を行うシステム
	//============================================================================
	class PrefabSystem {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		PrefabSystem() = default;
		~PrefabSystem() = default;

		// プレファブ保存
		bool SavePrefab(AssetDatabase& database, ECSWorld& world,
			const Entity& root, const std::string& prefabAssetPath) const;

		// プレファブ生成
		bool InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem, ECSWorld& world,
			AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc = {}) const;
		bool InstantiatePrefabFromPath(AssetDatabase& database, HierarchySystem& hierarchySystem, ECSWorld& world,
			const std::string& prefabAssetPath, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc = {}) const;
	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// プレファブのルートからその子孫全てを収集する
		std::vector<Entity> CollectSubtree(ECSWorld& world, const Entity& root) const;
		// 収集したエンティティ群からローカルファイルIDを割り当てる
		UUID AllocateUniqueLocalFileID(ECSWorld& world) const;
		// プレファブのルートエンティティの名前を生成する
		std::string BuildDefaultPrefabName(ECSWorld& world, const Entity& root, const std::string& prefabAssetPath) const;
	};
} // Engine