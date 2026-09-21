#include "AssetGUID.h"

//============================================================================
//	include
//============================================================================
// c++
#include <random>
#include <limits>

namespace {

	uint64_t ParseHex64(std::string_view text, bool& valid) noexcept {

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
				valid = false;
				return 0;
			}
			value = (value << 4) | nibble;
		}
		return value;
	}
}

//============================================================================
//	AssetGUID classMethods
//============================================================================
bool Engine::AssetGUID::operator==(const AssetGUID& other) const noexcept {

	return high == other.high && low == other.low;
}

bool Engine::AssetGUID::operator!=(const AssetGUID& other) const noexcept {

	return !(*this == other);
}

bool Engine::AssetGUID::operator<(const AssetGUID& other) const noexcept {

	if (high != other.high) {
		return high < other.high;
	}
	return low < other.low;
}

Engine::AssetGUID::operator bool() const noexcept {

	return high != 0 || low != 0;
}

Engine::AssetGUID Engine::AssetGUID::New() {

	static thread_local std::mt19937_64 rng{ std::random_device{}() };
	std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

	AssetGUID guid{};
	do {
		guid.high = dist(rng);
		guid.low = dist(rng);
	} while (!guid);
	return guid;
}

std::string Engine::ToString(const AssetGUID& guid) {

	char buffer[33]{};
	static constexpr char kHex[] = "0123456789abcdef";

	for (int i = 0; i < 16; ++i) {
		const int shift = (15 - i) * 4;
		buffer[i] = kHex[(guid.high >> shift) & 0xF];
		buffer[i + 16] = kHex[(guid.low >> shift) & 0xF];
	}
	return std::string(buffer, 32);
}

std::optional<Engine::AssetGUID> Engine::TryParseAssetGUID32Hex(std::string_view text) noexcept {

	if (text.size() != 32) {
		return std::nullopt;
	}

	bool valid = true;
	const uint64_t high = ParseHex64(text.substr(0, 16), valid);
	const uint64_t low = ParseHex64(text.substr(16, 16), valid);
	if (!valid || (high == 0 && low == 0)) {
		return std::nullopt;
	}
	return AssetGUID{ high, low };
}

Engine::AssetGUID Engine::FromString32Hex(std::string_view text) noexcept {

	const std::optional<AssetGUID> parsed = TryParseAssetGUID32Hex(text);
	return parsed ? *parsed : AssetGUID{};
}

//============================================================================
//	AssetGUID classMethods
//============================================================================

namespace std {

	size_t hash<Engine::AssetGUID>::operator()(const Engine::AssetGUID& guid) const noexcept {

		const size_t highHash = std::hash<uint64_t>{}(guid.high);
		const size_t lowHash = std::hash<uint64_t>{}(guid.low);
		return highHash ^ (lowHash + 0x9e3779b97f4a7c15ull + (highHash << 6) + (highHash >> 2));
	}
}
