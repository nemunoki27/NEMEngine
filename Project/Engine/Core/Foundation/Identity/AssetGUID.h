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
	//	AssetGUID struct
	//	プロジェクトとパッケージのAssetを識別する128ビットの値
	//============================================================================
	struct AssetGUID {

		//--------- variables ----------------------------------------------------

		uint64_t high = 0; // 上位64ビット
		uint64_t low = 0;  // 下位64ビット

		//--------- functions ----------------------------------------------------

		// 識別子の一致を判定する
		bool operator==(const AssetGUID& other) const noexcept;
		// 識別子の不一致を判定する
		bool operator!=(const AssetGUID& other) const noexcept;
		// 上位と下位の順に大小を比較する
		bool operator<(const AssetGUID& other) const noexcept;
		// ゼロ以外の識別子か判定する
		explicit operator bool() const noexcept;

		// IDを生成する
		static AssetGUID New();
	};

	// AssetGUIDを32桁の16進数文字列に変換する
	std::string ToString(const AssetGUID& guid);

	// 32桁の16進数から読み取り、不正値とゼロは未取得を返す
	std::optional<AssetGUID> TryParseAssetGUID32Hex(std::string_view text) noexcept;

	// 32桁の16進数からAssetGUIDを生成し、不正値は無効値を返す
	AssetGUID FromString32Hex(std::string_view text) noexcept;

	static_assert(sizeof(AssetGUID) == 16);
}

//============================================================================
// std::hashの特殊化
//============================================================================
namespace std {

	template <>
	struct hash<Engine::AssetGUID> {
		size_t operator()(const Engine::AssetGUID& guid) const noexcept;
	};
}
