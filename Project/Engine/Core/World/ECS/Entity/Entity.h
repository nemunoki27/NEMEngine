#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>

namespace Engine {

	//============================================================================
	//	Entity struct
	//	エンティティを表し参照インデックスと破棄や再利用のための世代を所持する
	//============================================================================
	struct Entity {

		// 参照インデックス
		uint32_t index = 0xFFFFFFFF;
		// 破棄、再利用するための世代
		uint32_t generation = 0;

		// 比較演算子
		friend bool operator==(const Entity& entityA, const Entity& entityB) noexcept;
		friend bool operator!=(const Entity& entityA, const Entity& entityB) noexcept;

		// 有効なエンティティか
		constexpr bool IsValid() const noexcept { return index != 0xFFFFFFFF; }
		// 無効なエンティティを返す
		static constexpr Entity Null() noexcept { return Entity{}; }
	};
} // Engine