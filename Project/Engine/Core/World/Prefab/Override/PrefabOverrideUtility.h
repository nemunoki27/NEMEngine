#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideTypes.h>

// c++
#include <unordered_map>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	// front
	class AssetDatabase;
	class ECSWorld;
	class HierarchySystem;

	//============================================================================
	//	PrefabBaseEntity struct
	//	プレファブファイル内の1実体分のベース情報
	//============================================================================
	struct PrefabBaseEntity {

		// プレファブ内ローカルID
		UUID localFileID{};
		// プレファブ内での親ローカルID
		UUID parentLocalFileID{};
		// プレファブのルートか
		bool isRoot = false;
		// 型名から値へのコンポーネントマップ
		nlohmann::json components;
	};

	//============================================================================
	//	EntityOverrideInfo struct
	//	1エンティティ分のオーバーライド概要、インスペクター表示に使う
	//============================================================================
	struct EntityOverrideInfo {

		// プレファブインスタンスの一部か
		bool isPrefabInstance = false;
		// 束ねるインスタンスID
		UUID instanceID{};
		// プレファブ内ローカルID
		UUID prefabLocalFileID{};
		// 元プレファブアセット
		AssetID prefabAsset{};

		// オーバーライドされたプロパティ経路で型名から始まる
		std::vector<std::string> modifiedPaths;
		// 追加されたコンポーネント型名
		std::vector<std::string> addedComponentTypes;
		// 削除されたコンポーネント型名
		std::vector<std::string> removedComponentTypes;

		// オーバーライドが何か在るか
		bool HasAnyOverride() const { return !modifiedPaths.empty() || !addedComponentTypes.empty() || !removedComponentTypes.empty(); }
	};

	//============================================================================
	//	PrefabOverrideUtility namespace
	//	プレファブインスタンスの差分抽出と再生成
	//============================================================================
	namespace PrefabOverrideUtility {

		// 1エンティティのオーバーライド概要を抽出する、プレファブインスタンスでなければisPrefabInstanceはfalse
		EntityOverrideInfo CaptureEntityOverride(ECSWorld& world, const Entity& entity, AssetDatabase& database);

		// プレファブファイルを読み、プレファブ内ローカルIDからベース実体へのマップを作る
		std::unordered_map<UUID, PrefabBaseEntity> LoadPrefabBaseEntities(AssetDatabase& database,
			AssetID prefabAsset, UUID* outRootLocalFileID = nullptr);

		// LoadPrefabBaseEntitiesをファイル更新時刻でキャッシュして返す、毎フレーム呼ばれても更新が無ければ再読込しない
		const std::unordered_map<UUID, PrefabBaseEntity>& LoadPrefabBaseEntitiesCached(
			AssetDatabase& database, AssetID prefabAsset);
		// 保存直後に更新時刻が変化しない場合へ備えてキャッシュを破棄する
		void InvalidatePrefabBaseCache(AssetID prefabAsset);

		// インスタンスに属する全エンティティを集める
		std::vector<Entity> CollectInstanceEntities(ECSWorld& world, UUID instanceID);

		// ライブなインスタンスからベースとの差分を抽出する
		PrefabInstanceData CaptureInstance(ECSWorld& world, AssetDatabase& database, UUID instanceID,
			const std::unordered_map<UUID, PrefabBaseEntity>& base);

		// 階層からネストPrefabの所有関係を更新する
		void SynchronizeNestedPrefabOwnership(ECSWorld& world);

		// 薄いデータからインスタンスを生成または再生成し、生成したルートを返す
		Entity RebuildInstance(ECSWorld& world, AssetDatabase& database, HierarchySystem& hierarchySystem,
			const PrefabInstanceData& data, UUID sceneInstanceID, uint32_t nestedDepth = 0);

		// 指定ワールドの該当プレファブインスタンスを、oldBaseとの差分を保持しつつ現在のプレファブで作り直す
		bool PropagateToInstances(ECSWorld& world, AssetDatabase& database, HierarchySystem& hierarchySystem,
			AssetID prefabAsset, const std::unordered_map<UUID, PrefabBaseEntity>& oldBase);

		// 追加EntityのサブツリーをPrefabへ反映できるか
		bool CanPromoteAddedEntitySubtree(ECSWorld& world, const Entity& root, UUID instanceID);
		// 追加EntityのサブツリーをPrefabファイルへ追加しPrefab由来Entityへ昇格する
		bool PromoteAddedEntitySubtrees(nlohmann::json& prefabFileJson, ECSWorld& world,
			AssetID prefabAsset, UUID instanceID, const std::vector<Entity>& roots);

		// プレファブファイルJSONの指定実体へ差分を書き込むヘルパー、まとめて呼んで最後に保存する
		// pathは型名から始まるリーフ経路でTransform/position/xのような形
		bool SetPrefabEntityLeaf(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
			const std::string& path, const nlohmann::json& value);
		bool SetPrefabEntityComponent(nlohmann::json& prefabFileJson, UUID targetLocalFileID,
			const std::string& type, const nlohmann::json& value);
		bool RemovePrefabEntityComponent(nlohmann::json& prefabFileJson, UUID targetLocalFileID, const std::string& type);
	}

	//============================================================================
	//	PrefabInstanceData json変換
	//============================================================================
	nlohmann::json ToJson(const PrefabInstanceData& data);
	bool FromJson(const nlohmann::json& json, PrefabInstanceData& data);
} // Engine
