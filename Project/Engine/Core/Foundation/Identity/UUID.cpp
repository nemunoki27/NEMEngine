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

Engine::UUID Engine::FromString16Hex(const std::string& string) {

	// 16桁以外は無効なIDとする
	if (string.size() != 16) {
		return UUID{};
	}
	auto nib = [](char c)->uint64_t {
		if ('0' <= c && c <= '9') return c - '0';
		if ('a' <= c && c <= 'f') return 10 + (c - 'a');
		if ('A' <= c && c <= 'F') return 10 + (c - 'A');
		return 0;
		};
	uint64_t v = 0;
	for (char c : string) {
		v = (v << 4) | nib(c);
	}
	return UUID{ .value = v };
}