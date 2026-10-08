#include "AssetGUID.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>
#include "IdentityText.h"

// c++
#include <random>
#include <limits>

//============================================================================
//	AssetGUID classMethods
//============================================================================
bool Engine::AssetGUID::operator==(const AssetGUID& other) const noexcept {

	// 上位と下位の一致を判定
	return high == other.high && low == other.low;
}

bool Engine::AssetGUID::operator!=(const AssetGUID& other) const noexcept {

	// 一致判定を反転
	return !(*this == other);
}

bool Engine::AssetGUID::operator<(const AssetGUID& other) const noexcept {

	// 上位が同じ場合だけ下位を比較
	if (high != other.high) {
		return high < other.high;
	}
	return low < other.low;
}

Engine::AssetGUID::operator bool() const noexcept {

	// 全ビットがゼロの識別子を除外
	return high != 0 || low != 0;
}

Engine::AssetGUID Engine::AssetGUID::New() {

	// スレッドごとの乱数で有効な識別子を生成
	static thread_local std::mt19937_64 rng{std::random_device{}()};
	std::uniform_int_distribution<uint64_t> dist(0, std::numeric_limits<uint64_t>::max());

	AssetGUID guid{};
	do {
		guid.high = dist(rng);
		guid.low = dist(rng);
	} while (!guid);
	return guid;
}

std::string Engine::ToString(const AssetGUID& guid) {

	// 上位と下位を同じ桁順で連結
	char buffer[32];
	IdentityText::WriteHex64(guid.high, std::span<char, 16>(buffer, 16));
	IdentityText::WriteHex64(guid.low, std::span<char, 16>(buffer + 16, 16));
	return std::string(buffer, 32);
}

std::optional<Engine::AssetGUID> Engine::TryParseAssetGUID32Hex(std::string_view text) noexcept {

	if (text.size() != 32) {
		return std::nullopt;
	}

	// 上下の解析結果と無効値を確認
	const auto high = IdentityText::TryParseHex64(text.substr(0, 16));
	const auto low = IdentityText::TryParseHex64(text.substr(16, 16));
	if (!high || !low || (*high == 0 && *low == 0)) {
		return std::nullopt;
	}
	return AssetGUID{*high, *low};
}

Engine::AssetGUID Engine::FromString32Hex(std::string_view text) noexcept {

	// 不正な入力は無効値を返す
	const std::optional<AssetGUID> parsed = TryParseAssetGUID32Hex(text);
	return parsed ? *parsed : AssetGUID{};
}

//============================================================================
//	AssetGUID classMethods
//============================================================================

namespace std {

	size_t hash<Engine::AssetGUID>::operator()(const Engine::AssetGUID& guid) const noexcept {

		// 上下のハッシュを合成
		const size_t highHash = std::hash<uint64_t>{}(guid.high);
		const size_t lowHash = std::hash<uint64_t>{}(guid.low);
		return Engine::Algorithm::MixHash(highHash, lowHash);
	}
}
