#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Assets/AssetTypes.h>
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <string>
#include <vector>
#include <utility>
// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	PrefabOverride structures
	//	プレファブインスタンスの差分を薄く表現するためのデータ群
	//============================================================================

	// プロパティ1つ分のオーバーライド差分
	struct PrefabPropertyModification {

		// 対象エンティティのプレファブ内ローカルID
		UUID target{};
		// コンポーネント型名から始まるリーフ経路でTransform/position/xのような形
		std::string path;
		// インスタンス側の値
		nlohmann::json value;
	};

	// コンポーネント単位の追加または削除
	struct PrefabComponentModification {

		// 対象エンティティのプレファブ内ローカルID
		UUID target{};
		// コンポーネント型名
		std::string type;
		// 追加時のみ使う値で削除時は空
		nlohmann::json value;
	};

	// 親子関係の構造オーバーライド
	struct PrefabHierarchyModification {

		// 対象エンティティのプレファブ内ローカルID
		UUID target{};
		// 親オーバーライドが有効か
		bool hasParentOverride = false;
		// インスタンス内の新しい親のプレファブ内ローカルID
		UUID newParentPrefabLocalFileID{};
		// インスタンス外の実体を親にする場合のシーンローカルID
		UUID externalParentSceneLocalFileID{};
		// 兄弟順のオーバーライドが有効か
		bool hasSiblingOrder = false;
		// 兄弟順の値
		int32_t siblingOrder = 0;
	};

	// プレファブ由来でない追加実体
	struct PrefabAddedEntity {

		// シーン内ローカルID
		UUID sceneLocalFileID{};
		// ぶら下げ先の親のシーン内ローカルID
		UUID parentSceneLocalFileID{};
		// SceneObjectやHierarchyを含む完全なコンポーネント情報
		nlohmann::json components;
	};

	//============================================================================
	//	PrefabInstanceData struct
	//	1プレファブインスタンスの薄い保存/復元用データ
	//============================================================================
	struct PrefabInstanceData {

		// 元になったプレファブアセット
		AssetID prefabAsset{};
		// インスタンスを束ねるID
		UUID instanceID{};
		// インスタンスrootを別実体の子にしている場合の親シーンローカルID
		UUID rootParentSceneLocalFileID{};

		// プレファブ内ローカルIDからシーン内ローカルIDへの対応
		std::vector<std::pair<UUID, UUID>> entityMap;

		// プロパティ値のオーバーライド
		std::vector<PrefabPropertyModification> modifications;
		// 追加されたコンポーネント
		std::vector<PrefabComponentModification> addedComponents;
		// 削除されたコンポーネント
		std::vector<PrefabComponentModification> removedComponents;
		// 親子の構造オーバーライド
		std::vector<PrefabHierarchyModification> hierarchyModifications;
		// プレファブから取り除かれた実体のプレファブ内ローカルID
		std::vector<UUID> removedEntities;
		// 追加された実体
		std::vector<PrefabAddedEntity> addedEntities;

		// オーバーライドが何も無いか、薄い保存の要否判定に使う
		bool IsEmpty() const {
			return modifications.empty() && addedComponents.empty() && removedComponents.empty() &&
				hierarchyModifications.empty() && removedEntities.empty() && addedEntities.empty() &&
				!rootParentSceneLocalFileID;
		}
	};
} // Engine
