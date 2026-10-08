#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <string_view>

namespace Engine::Algorithm {

	// 値を既存のハッシュへ合成する
	void HashCombine(uint64_t& hash, uint64_t value);

	// bit移動で次の値を既存Hashへ混ぜる
	uint64_t MixHash(uint64_t seed, uint64_t value) noexcept;
	// 文字列のByte列を既存Hashへ混ぜる
	uint64_t MixHashString(uint64_t seed, std::string_view value) noexcept;
}
