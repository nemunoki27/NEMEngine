#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <random>
#include <limits>
#include <functional>
#include <cstddef>

namespace Engine {

	//============================================================================
	//	UUID
	//	64bitの永続UID。0は無効値。
	//	アセット・Scene内部IDなどの安定識別に使用する(RFC4122の128bit UUIDではない)。
	//============================================================================
	struct UUID {

		// 0は無効なIDとする
		uint64_t value = 0;

		// 比較演算子
		bool operator==(const UUID& other) const noexcept;
		bool operator!=(const UUID& other) const noexcept;
		explicit operator bool() const noexcept;

		// IDを生成する
		static UUID New();
	};

	// UUIDを16桁の16進数文字列に変換する関数
	std::string ToString(const UUID& id);

	// 16桁の16進数文字列を厳密にパースする。
	// 長さが16でない/16進以外の文字を含む/値が0の場合はnulloptを返す。
	std::optional<UUID> TryParseUUID16Hex(std::string_view text) noexcept;

	// 16桁の16進数文字列からUUIDを生成する。不正な入力は無効値({})を返す。
	// 厳密にエラーを区別したい場合はTryParseUUID16Hexを使う(本関数も内部でそれを呼ぶ)。
	UUID FromString16Hex(std::string_view text) noexcept;
} // Engine

//============================================================================
// std::hashの特殊化
//============================================================================
namespace std {

	template<>
	struct hash<Engine::UUID> {
		size_t operator()(const Engine::UUID& id) const noexcept {
			return std::hash<uint64_t>{}(id.value);
		}
	};

}