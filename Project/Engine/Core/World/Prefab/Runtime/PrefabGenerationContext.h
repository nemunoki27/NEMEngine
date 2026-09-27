#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Identity/UUID.h>

// c++
#include <unordered_set>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	class HierarchySystem;
	struct PrefabInstanceData;

	// ネストを含む1回のPrefab生成が参照する処理対象
	struct PrefabGenerationContext {

		PrefabGenerationContext(AssetDatabase& database, HierarchySystem& hierarchySystem, ECSWorld& world);

		// 生成中の予約を含めてSceneローカルIDを採番する
		UUID AllocateLocalFileID();
		// 保存済みのSceneローカルIDを予約する
		void ReserveLocalFileID(UUID localFileID);
		// ネストと追加実体の保存IDも先に予約する
		void ReserveInstanceLocalFileIDs(const PrefabInstanceData& data);

		AssetDatabase& database;
		HierarchySystem& hierarchySystem;
		ECSWorld& world;
	private:
		std::unordered_set<UUID> reservedLocalFileIDs_;
	};
} // Engine
