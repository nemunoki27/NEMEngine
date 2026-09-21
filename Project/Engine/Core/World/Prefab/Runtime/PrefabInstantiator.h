#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Prefab/Runtime/PrefabInstantiation.h>
#include <Engine/Core/World/Prefab/Runtime/PrefabGenerationContext.h>

namespace Engine {

	class AssetDatabase;
	class ECSWorld;
	class HierarchySystem;

	//============================================================================
	//	PrefabInstantiator class
	//	プレファブの実体と参照対応を構築する
	//============================================================================
	class PrefabInstantiator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// Prefab文書から実体と参照対応を生成する
		static bool InstantiatePrefab(AssetDatabase& database, HierarchySystem& hierarchySystem,
			ECSWorld& world, AssetID prefabAsset, PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc);

		// ネスト生成と同じ参照対象を共有して生成する
		static bool InstantiatePrefab(PrefabGenerationContext& context, AssetID prefabAsset,
			PrefabInstantiateResult& outResult, const PrefabInstantiateDesc& desc);

	private:
		//========================================================================
		//	private Methods
		//========================================================================

		// ワールド内で重複しないSceneローカルIDを採番する
		static Engine::UUID AllocateUniqueLocalFileID(ECSWorld& world);
	};
} // Engine
