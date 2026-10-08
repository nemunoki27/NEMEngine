#pragma once

//============================================================================
//	include
//============================================================================
#include "Entity.h"

// c++
#include <cstddef>

namespace Engine {

	class ECSWorld;
	//============================================================================
	//	WorldEntityKey struct
	//	Worldと実行時Entityを組み合わせた検索キー
	//============================================================================
	struct WorldEntityKey {

		const ECSWorld* world = nullptr;
		Entity entity = Entity::Null();

		// WorldとEntityの世代まで一致するか
		bool operator==(const WorldEntityKey& other) const noexcept = default;
	};

	//============================================================================
	//	WorldEntityKeyHash struct
	//	WorldとEntityの検索用Hashを計算
	//============================================================================
	struct WorldEntityKeyHash {

		// WorldとEntityの全識別子をHashへ反映
		size_t operator()(const WorldEntityKey& key) const noexcept;
	};
}
