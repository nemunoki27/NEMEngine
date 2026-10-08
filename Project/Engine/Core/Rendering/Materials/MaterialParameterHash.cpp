#include "MaterialParameterHash.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>

// c++
#include <bit>
#include <type_traits>
#include <variant>

namespace Engine::MaterialParameterHash {

	uint64_t HashValue(const MaterialParameterValue& parameter) {

		// 型番号を先に混ぜ、同じ数値の別型を区別する
		uint64_t hash = parameter.value.index();
		const auto appendFloat = [&](float value) {
			hash = Algorithm::MixHash(hash, std::bit_cast<uint32_t>(value));
			};
		// 浮動小数点はbit列をそのまま混ぜる
		std::visit([&](const auto& value) {
			using ValueType = std::decay_t<decltype(value)>;

			if constexpr (std::is_same_v<ValueType, float>) {
				appendFloat(value);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector2>) {
				appendFloat(value.x);
				appendFloat(value.y);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector3>) {
				appendFloat(value.x);
				appendFloat(value.y);
				appendFloat(value.z);
			} else if constexpr (std::is_same_v<ValueType, Engine::Vector4>) {
				appendFloat(value.x);
				appendFloat(value.y);
				appendFloat(value.z);
				appendFloat(value.w);
			} else if constexpr (std::is_same_v<ValueType, Engine::Color4>) {
				appendFloat(value.r);
				appendFloat(value.g);
				appendFloat(value.b);
				appendFloat(value.a);
			} else if constexpr (std::is_same_v<ValueType, Engine::AssetID>) {
				hash = Algorithm::MixHash(hash, value.high);
				hash = Algorithm::MixHash(hash, value.low);
			} else {
				hash = Algorithm::MixHash(hash, static_cast<uint64_t>(value));
			}
			}, parameter.value);
		return hash;
	}

} // Engine::MaterialParameterHash
