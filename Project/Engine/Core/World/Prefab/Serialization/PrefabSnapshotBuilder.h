#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiation.h>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	class HierarchySystem;

	//============================================================================
	//	PrefabSnapshotBuilder class
	//	プレファブの保存データを構築する
	//============================================================================
	class PrefabSnapshotBuilder {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 明示した実体群からPrefabを保存する
		static bool SavePrefabFromEntities(AssetDatabase& database, ECSWorld& world, const Entity& root,
			const std::vector<Entity>& entities, const std::string& prefabAssetPath, UUID prefabInstanceID);
		// 保存前にGUIDを揃えてPrefab文書を構築する
		static bool BuildDocument(AssetDatabase& database, ECSWorld& world, const Entity& root,
			const std::vector<Entity>& entities, const std::string& prefabAssetPath, UUID prefabInstanceID, AssetID prefabAsset,
			nlohmann::json& fileJson);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		//--------- functions ----------------------------------------------------

		// 保存ルートの表示名を決める
		static std::string BuildDefaultPrefabName(ECSWorld& world, const Entity& root, const std::string& prefabAssetPath);
	};
} // Engine
