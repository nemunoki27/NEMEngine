#include "HashUtility.h"

//============================================================================
//	include
//============================================================================

namespace Engine::Algorithm {

	void HashCombine(uint64_t& hash, uint64_t value) {

		// XORと乗算で次の値を合成
		hash ^= value;
		hash *= 1099511628211ull;
	}

	uint64_t MixHash(uint64_t seed, uint64_t value) noexcept {

		return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2));
	}

	uint64_t MixHashString(uint64_t seed, std::string_view value) noexcept {

		// 文字コードを変換せずByte単位で混ぜる
		for (const char character : value) {
			seed = MixHash(seed, static_cast<uint8_t>(character));
		}
		return seed;
	}
}
