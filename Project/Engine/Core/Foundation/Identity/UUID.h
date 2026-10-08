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
	//	UUID struct
	//	Scene内部で使用する64ビットの永続識別子
	//============================================================================
	struct UUID {

		//--------- variables ----------------------------------------------------

		// 0は無効なIDとする
		uint64_t value = 0;

		//--------- functions ----------------------------------------------------

		// 識別子の一致を判定する
		bool operator==(const UUID& other) const noexcept;
		// 識別子の不一致を判定する
		bool operator!=(const UUID& other) const noexcept;
		// ゼロ以外の識別子か判定する
		explicit operator bool() const noexcept;

		// IDを生成する
		static UUID New();
	};

	// UUIDを16桁の16進数文字列に変換する
	std::string ToString(const UUID& id);

	// 16桁の16進数から読み取り、不正値とゼロは未取得を返す
	std::optional<UUID> TryParseUUID16Hex(std::string_view text) noexcept;

	// 16桁の16進数からUUIDを生成し、不正値は無効値を返す
	UUID FromString16Hex(std::string_view text) noexcept;
	static_assert(sizeof(UUID) == 8);
}

//============================================================================
// std::hashの特殊化
//============================================================================
namespace std {

	template <>
	struct hash<Engine::UUID> {
		size_t operator()(const Engine::UUID& id) const noexcept { return std::hash<uint64_t>{}(id.value); }
	};

}
