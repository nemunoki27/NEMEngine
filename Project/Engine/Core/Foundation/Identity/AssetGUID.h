#pragma once

//============================================================================
//	include
//============================================================================
// c++
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <functional>
#include <cstddef>

namespace Engine {

	//============================================================================
	//	AssetGUID
	// アセットをプロジェクト、Package間で一意に識別する128bit ID
	//============================================================================
	struct AssetGUID {

		uint64_t high = 0;
		uint64_t low = 0;

		// 比較演算子
		bool operator==(const AssetGUID& other) const noexcept;
		bool operator!=(const AssetGUID& other) const noexcept;
		bool operator<(const AssetGUID& other) const noexcept;
		explicit operator bool() const noexcept;

		// IDを生成する
		static AssetGUID New();
	};

	// AssetGUIDを32桁の16進数文字列に変換する関数
	std::string ToString(const AssetGUID& guid);

	// 32桁の16進数文字列を厳密にパースする
	std::optional<AssetGUID> TryParseAssetGUID32Hex(std::string_view text) noexcept;

	// 32桁の16進数文字列からAssetGUIDを生成する、不正な入力は無効値の{}を返す
	AssetGUID FromString32Hex(std::string_view text) noexcept;

	static_assert(sizeof(AssetGUID) == 16);
} // Engine

//============================================================================
// std::hashの特殊化
//============================================================================
namespace std {

	template<>
	struct hash<Engine::AssetGUID> {
		size_t operator()(const Engine::AssetGUID& guid) const noexcept {

			const size_t highHash = std::hash<uint64_t>{}(guid.high);
			const size_t lowHash = std::hash<uint64_t>{}(guid.low);
			return highHash ^ (lowHash + 0x9e3779b97f4a7c15ull + (highHash << 6) + (highHash >> 2));
		}
	};
}
