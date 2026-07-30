#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>

namespace Engine {

	//============================================================================
	//	BehaviorHandle struct
	//	Behaviorインスタンス参照用
	//============================================================================
	struct BehaviorHandle {

		// 参照インデックス
		uint32_t index = 0xFFFFFFFF;
		// 破棄、再利用するための世代
		uint32_t generation = 0;

		bool operator==(const BehaviorHandle& other) const noexcept = default;

		// 有効なハンドルか
		constexpr bool IsValid() const noexcept { return index != 0xFFFFFFFF; }
		// 無効なハンドルを返す
		static constexpr BehaviorHandle Null() noexcept { return BehaviorHandle{}; }
	};
} // Engine
