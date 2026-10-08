#include "ScopedValueTests.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/ScopedValue.h>

// c++
#include <stdexcept>
#include <type_traits>

bool NEMTests::TestScopedValueContracts() {

	static_assert(!std::is_copy_constructible_v<Engine::ScopedValue<bool>>);
	static_assert(!std::is_move_constructible_v<Engine::ScopedValue<bool>>);
	static_assert(std::is_nothrow_destructible_v<Engine::ScopedValue<bool>>);

	// 内側の失敗では外側の値へ戻す
	bool enabled = false;
	{
		Engine::ScopedValue outer(enabled, true);
		if (!enabled) {
			return false;
		}
		try {
			Engine::ScopedValue inner(enabled, false);
			if (enabled) {
				return false;
			}
			throw std::runtime_error("scoped value test");
		} catch (const std::runtime_error&) {
			if (!enabled) {
				return false;
			}
		}
	}
	if (enabled) {
		return false;
	}

	// 借用先が破棄される前に元の参照へ戻す
	const int original = 1;
	const int* current = &original;
	try {
		const int temporary = 2;
		Engine::ScopedValue reference(current, &temporary);
		if (current != &temporary) {
			return false;
		}
		throw std::runtime_error("scoped reference test");
	} catch (const std::runtime_error&) {
		return current == &original;
	}
}
