#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace Engine::IdentityText {

	// 16桁の16進数を符号なし整数として読み取る
	std::optional<uint64_t> TryParseHex64(std::string_view text) noexcept;
	// 整数を小文字の16進数16桁へ変換する
	void WriteHex64(uint64_t value, std::span<char, 16> output) noexcept;
}
