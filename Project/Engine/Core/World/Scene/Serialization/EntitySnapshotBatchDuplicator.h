#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Scene/Serialization/EntityTreeSnapshot.h>

// c++
#include <span>

namespace Engine {

	//============================================================================
	//	EntitySnapshotBatchDuplicator class
	//	複数階層を同じ複製範囲として対応付ける
	//============================================================================
	class EntitySnapshotBatchDuplicator {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		// 全階層のIDを確定してから範囲内参照を変換する
		static std::vector<EntityTreeSnapshot> Build(std::span<const EntityTreeSnapshot> sources);
	};
}
