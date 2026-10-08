#include "UUID.h"

//============================================================================
//	include
//============================================================================
#include "IdentityText.h"

// c++
#include <limits>
#include <random>

//============================================================================
//	UUID classMethods
//============================================================================
bool Engine::UUID::operator==(const UUID& other) const noexcept {

	// 識別値の一致を判定
	return value == other.value;
}

bool Engine::UUID::operator!=(const UUID& other) const noexcept {

	// 識別値の不一致を判定
	return value != other.value;
}

Engine::UUID::operator bool() const noexcept {

	// ゼロの識別子を除外
	return value != 0;
}

Engine::UUID Engine::UUID::New() {

	// スレッドごとの乱数で有効な識別子を生成
	static thread_local std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<uint64_t> dist(1, std::numeric_limits<uint64_t>::max());
	return UUID{dist(rng)};
}

std::string Engine::ToString(const UUID& id) {

	// 先頭のゼロを含む16桁を出力
	char buffer[16];
	IdentityText::WriteHex64(id.value, buffer);
	return std::string(buffer, 16);
}

std::optional<Engine::UUID> Engine::TryParseUUID16Hex(std::string_view text) noexcept {

	// 共通の16進数変換後に無効値を除外
	const auto value = IdentityText::TryParseHex64(text);
	return value && *value != 0 ? std::optional<UUID>(UUID{*value}) : std::nullopt;
}

Engine::UUID Engine::FromString16Hex(std::string_view text) noexcept {

	// 不正な入力は無効値を返す
	const std::optional<UUID> parsed = TryParseUUID16Hex(text);
	return parsed ? *parsed : UUID{};
}