#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/World/ECSWorld.h>
#include <Engine/Core/UUID/UUID.h>
#include <Engine/Core/Asset/AssetTypes.h>

// c++
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	// front
	struct EditorCommandContext;

	//============================================================================
	//	EditorEntitySnapshot structures
	//============================================================================

	// エンティティのスナップショット情報
	struct SerializedEntitySnapshot {

		UUID stableUUID{};
		nlohmann::json components = nlohmann::json::object();
	};

	// エンティティツリーのスナップショット情報
	struct EditorEntityTreeSnapshot {

		// スナップショットのルートエンティティUUID
		UUID rootStableUUID{};
		// スナップショット化されたエンティティ群
		std::vector<SerializedEntitySnapshot> entities;

		// ルートが属していたランタイム情報
		UUID ownerSceneInstanceID{};
		AssetID ownerSourceAsset{};

		// スナップショットをクリアする
		void Clear();

		bool IsEmpty() const { return entities.empty(); }
	};

	//============================================================================
	//	EditorEntitySnapshotUtility namespace
	//============================================================================
	namespace EditorEntitySnapshotUtility {

		// ルートを含むサブツリーを収集する
		std::vector<Entity> CollectSubtreeEntities(ECSWorld& world, const Entity& root);

		// サブツリーをjsonスナップショット化する
		void CaptureSubtree(ECSWorld& world, const Entity& root, EditorEntityTreeSnapshot& outSnapshot);

		// スナップショットからエンティティ群を復元する
		std::vector<Entity> RestoreSubtree(ECSWorld& world, const EditorEntityTreeSnapshot& snapshot);
		// サブツリーを破棄する
		void DestroySubtree(ECSWorld& world, const Entity& root);

		// スナップショットの所属先のランタイム状態を補完する
		void FillMissingOwnerRuntimeState(const EditorCommandContext& context,
			ECSWorld& world, UUID parentStableUUID, EditorEntityTreeSnapshot& snapshot);
	}
} // Engine