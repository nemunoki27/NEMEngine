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
	//	PrefabOwnership class
	//	プレファブの所属とリンク解除を扱う
	//============================================================================
	class PrefabOwnership {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 実体へPrefabの所属情報を設定する
		static void SetPrefabLink(ECSWorld& world, const Entity& entity, AssetID prefabAsset,
			UUID prefabLocalFileID, UUID prefabInstanceID, bool isPrefabRoot,
			UUID ownerPrefabInstanceID, UUID nestedSlotID, bool isPrefabAssetNested);

		// サブツリーへPrefabの所属情報を設定する
		static Engine::UUID SetPrefabLinkToSubtree(ECSWorld& world, const Entity& root,
			AssetID prefabAsset, UUID prefabInstanceID);

		// 指定範囲のPrefab所属を解除する
		static bool UnpackPrefabInstance(
			ECSWorld& world, const Entity& root, PrefabUnpackMode mode);
	};
} // Engine
