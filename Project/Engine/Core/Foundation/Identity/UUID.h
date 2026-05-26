#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <cstdint>
#include <string>
#include <random>
#include <limits>
#include <functional>
#include <cstddef>

namespace Engine {

	//============================================================================
	//	UUID
	//	シーン内で永続的に一意なIDを生成する
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

	// 16桁の16進数文字列からUUIDを生成する関数
	UUID FromString16Hex(const std::string& string);
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