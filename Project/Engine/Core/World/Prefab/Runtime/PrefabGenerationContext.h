#pragma once

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	class HierarchySystem;

	// ネストを含む1回のPrefab生成が参照する処理対象
	struct PrefabGenerationContext {

		AssetDatabase& database;
		HierarchySystem& hierarchySystem;
		ECSWorld& world;
	};
} // Engine
