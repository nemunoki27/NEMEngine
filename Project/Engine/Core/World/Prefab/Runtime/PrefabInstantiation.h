#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/World/Prefab/Override/PrefabOverrideTypes.h>

// c++
#include <unordered_map>
#include <vector>

namespace Engine {

	//============================================================================
	//	PrefabUnpackMode enum class
	//============================================================================
	enum class PrefabUnpackMode :
		uint8_t {

		OutermostRoot,
		Completely,
	};

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

} // Engine
