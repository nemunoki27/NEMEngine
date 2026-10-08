#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/EntityTreeSnapshot.h>

// c++
#include <span>

namespace Engine {

	// 新しい階層を配置するScene
	struct EntitySnapshotScene {

		UUID instanceID{};
		AssetID asset{};
	};

	//============================================================================
	//	EntitySnapshotPlacement class
	//	生成前にScene所属と内部参照のAssetを揃える
	//============================================================================
	class EntitySnapshotPlacement {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 通常Entityの所属を変更しPrefabの参照元は維持する
		static void Apply(std::span<EntityTreeSnapshot> snapshots, std::span<const EntitySnapshotScene> destinations);
	};
}
