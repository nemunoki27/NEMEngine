#include "UUID.h"

//============================================================================
//	UUID classMethods
//============================================================================
bool Engine::UUID::operator==(const UUID& other) const noexcept {

	return value == other.value;
}

bool Engine::UUID::operator!=(const UUID& other) const noexcept {

	return value != other.value;
}

Engine::UUID::operator bool() const noexcept {

	return value != 0;
}

Engine::UUID Engine::UUID::New() {

	static thread_local std::mt19937_64 rng{ std::random_device{}() };
	std::uniform_int_distribution<uint64_t> dist(1, std::numeric_limits<uint64_t>::max());
	return UUID{ dist(rng) };
}

std::string Engine::ToString(const UUID& id) {

	char buf[17]{};
	static const char* hex = "0123456789abcdef";
	for (int i = 0; i < 16; ++i) {

		int shift = (15 - i) * 4;
		buf[i] = hex[(id.value >> shift) & 0xF];
	}
	return std::string(buf, 16);
}

std::optional<Engine::UUID> Engine::TryParseUUID16Hex(std::string_view text) noexcept {

	// 16桁以外は無効
	if (text.size() != 16) {
		return std::nullopt;
	}

	uint64_t value = 0;
	for (char c : text) {

		uint64_t nibble = 0;
		if ('0' <= c && c <= '9') {
			nibble = static_cast<uint64_t>(c - '0');
		} else if ('a' <= c && c <= 'f') {
			nibble = static_cast<uint64_t>(10 + (c - 'a'));
		} else if ('A' <= c && c <= 'F') {
			nibble = static_cast<uint64_t>(10 + (c - 'A'));
		} else {
			// 16進以外の文字は不正
			return std::nullopt;
		}
		value = (value << 4) | nibble;
	}

	// 0は無効値とする
	if (value == 0) {
		return std::nullopt;
	}
	return UUID{ value };
}

Engine::UUID Engine::FromString16Hex(std::string_view text) noexcept {

	// パースの実体はTryParseUUID16Hexに一本化し、不正な入力は無効値を返す
	const std::optional<UUID> parsed = TryParseUUID16Hex(text);
	return parsed ? *parsed : UUID{};
}