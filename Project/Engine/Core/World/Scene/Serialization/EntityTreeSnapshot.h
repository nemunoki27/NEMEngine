#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/Entity/Entity.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	class ECSWorld;

	// コピー時に解決した保存参照の実体
	struct EntityReferenceTargetSnapshot {

		AssetID sourceAsset{};
		UUID localFileID{};
		UUID targetStableUUID{};
	};

	// Entityの保存値と実体ID
	struct SerializedEntitySnapshot {

		UUID stableUUID{};
		nlohmann::json components = nlohmann::json::object();
		UUID sceneInstanceID{};
		AssetID sourceAsset{};
		std::vector<EntityReferenceTargetSnapshot> referenceTargets;
		bool referencesCaptured = false;
	};

	// 階層の実体と所属を保持する保存データ
	struct EntityTreeSnapshot {

		UUID rootStableUUID{};
		std::vector<SerializedEntitySnapshot> entities;
		UUID ownerSceneInstanceID{};
		AssetID ownerSourceAsset{};

		// 保存値と所属を初期化する
		void Clear();
		bool IsEmpty() const { return entities.empty(); }
	};

	//============================================================================
	//	EntitySnapshotUtility namespace
	//============================================================================
	namespace EntitySnapshotUtility {

		// ルート以下の保存値を取得する
		void CaptureSubtree(ECSWorld& world, const Entity& root, EntityTreeSnapshot& outSnapshot);
		// 保存参照の解決結果をコピー時点で固定する
		void CaptureReferenceTargets(ECSWorld& world, EntityTreeSnapshot& snapshot);
		// 全実体の復元に成功した場合だけ結果を返す
		std::vector<Entity> RestoreSubtree(ECSWorld& world, const EntityTreeSnapshot& snapshot);
	}
}
