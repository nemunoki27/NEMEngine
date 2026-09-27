#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Scene/Serialization/EntityTreeSnapshot.h>
#include <Engine/Core/Foundation/Identity/UUID.h>
#include <Engine/Core/Assets/AssetTypes.h>

// c++
#include <span>
#include <vector>
// json
#include <json.hpp>

namespace Engine {

	// front
	struct EditorCommandContext;

	//============================================================================
	//	EditorEntitySnapshot structures
	//============================================================================
	using EditorEntityTreeSnapshot = EntityTreeSnapshot;

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
		// 所属・階層・選択まで復元して操作を確定する
		Entity RestoreCommandSnapshot(EditorCommandContext& context, const EditorEntityTreeSnapshot& snapshot);
		// 復元直後のランタイム状態を補正する
		void RefreshRestoredRuntimeState(const EditorCommandContext& context, ECSWorld& world,
			const EditorEntityTreeSnapshot& snapshot, std::span<const Entity> restoredEntities);
		// サブツリーを破棄する
		void DestroySubtree(ECSWorld& world, const Entity& root);

		// スナップショットの所属先のランタイム状態を補完する
		void FillMissingOwnerRuntimeState(const EditorCommandContext& context,
			ECSWorld& world, UUID parentStableUUID, EditorEntityTreeSnapshot& snapshot);
	}
} // Engine
