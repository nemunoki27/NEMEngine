#include "IdentityText.h"

//============================================================================
//	include
//============================================================================
// c++
#include <charconv>

//============================================================================
//	IdentityText namespaceMethods
//============================================================================
std::optional<uint64_t> Engine::IdentityText::TryParseHex64(std::string_view text) noexcept {

	// 桁数と末尾までの変換を確認
	if (text.size() != 16) {
		return std::nullopt;
	}
	uint64_t value = 0;
	const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
	if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
		return std::nullopt;
	}
	return value;
}

void Engine::IdentityText::WriteHex64(uint64_t value, std::span<char, 16> output) noexcept {

	// 上位桁から書き込み、先頭のゼロも維持
	constexpr char kHex[] = "0123456789abcdef";
	for (size_t index = 0; index < output.size(); ++index) {

		const size_t shift = (15 - index) * 4;
		output[index] = kHex[(value >> shift) & 0xf];
	}
}
