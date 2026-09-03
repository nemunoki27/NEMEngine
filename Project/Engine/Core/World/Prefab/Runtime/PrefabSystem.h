#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/Database/AssetDatabase.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideTypes.h>
#include <Engine/Core/World/Prefab/Serialization/PrefabHeader.h>

// c++
#include <filesystem>
#include <vector>
#include <utility>

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

		// 有効ならインスタンスIDを新規採番せずこの値を使う、薄い保存からの復元で同一性を保つ
		UUID forcedInstanceID{};

		// 生成したルートの名前を.prefabのベース名にするか、新規生成時のみtrueにしシーン復元では既存名を尊重する
		bool renameRootToPrefabName = false;
		// プレファブ内ローカルIDからシーンローカルIDへの対応で、薄い保存からの復元時に同一性を保つ
		// 非所有ポインタで参照、対応が無いローカルIDは従来通り新規採番する
		const std::vector<std::pair<UUID, UUID>>* localFileIDRemap = nullptr;
		// プレファブ内ローカルIDからEntityの安定UUIDへの対応、インスタンス再構築時だけ使用する
		const std::vector<std::pair<UUID, UUID>>* stableUUIDRemap = nullptr;
		// シーン保存から復元するネストPrefab差分、指定が無ければPrefabアセットの初期値を使う
		const std::vector<PrefabInstanceData>* nestedInstanceRemap = nullptr;
		// シーン上で削除された親Prefab由来のネストスロット
		const std::vector<UUID>* removedNestedSlots = nullptr;
		// ネスト元のPrefabインスタンスとスロット
		UUID ownerPrefabInstanceID{};
		UUID nestedSlotID{};
		bool isPrefabAssetNested = false;
		// 循環参照で無限生成しないためのネスト深度
		uint32_t nestedDepth = 0;
		// Prefab編集時にネストPrefabの保存IDを維持するか
		bool preserveNestedLocalFileIDs = false;
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

		// プレファブ生成
		bool InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem, ECSWorld& world,
			AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc = {}) const;
		bool InstantiatePrefabFromPath(AssetDatabase& database, HierarchySystem& hierarchySystem, ECSWorld& world,
			const std::string& prefabAssetPath, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc = {}) const;
	private:
		//============================================================================
		//	private Methods
		//============================================================================

		//--------- functions ----------------------------------------------------

		// 収集したエンティティ群からローカルファイルIDを割り当てる
		UUID AllocateUniqueLocalFileID(ECSWorld& world) const;
		// プレファブのルートエンティティの名前を生成する
		std::string BuildDefaultPrefabName(ECSWorld& world, const Entity& root, const std::string& prefabAssetPath) const;
	};
} // Engine
