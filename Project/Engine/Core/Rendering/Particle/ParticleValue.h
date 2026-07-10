#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Random/RandomGenerator.h>
#include <Engine/Core/Foundation/Math/Vector3.h>

// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	ParticleValue
	//	定数かランダム範囲かを切り替えられる値、エフェクトの調整パラメータで共用する
	//============================================================================
	// 値の決め方
	enum class ParticleValueType :
		uint8_t {

		Constant,
		Random,
	};

	template <typename T>
	struct ParticleValue {

		// 値の決め方
		ParticleValueType type = ParticleValueType::Constant;
		// 定数値
		T constant{};
		// ランダム範囲
		T min{};
		T max{};

		ParticleValue() = default;
		ParticleValue(const T& value) : constant(value), min(value), max(value) {}

		// 設定に応じた値を取得する
		T Sample() const {

			if (type == ParticleValueType::Random) {
				return RandomGenerator::Generate(min, max);
			}
			return constant;
		}
	};

	// json変換
	template <typename T>
	inline void to_json(nlohmann::json& out, const ParticleValue<T>& value) {

		out["type"] = value.type == ParticleValueType::Random ? "Random" : "Constant";
		out["constant"] = value.constant;
		out["min"] = value.min;
		out["max"] = value.max;
	}

	template <typename T>
	inline void from_json(const nlohmann::json& in, ParticleValue<T>& value) {

		if (!in.is_object()) {
			return;
		}
		value.type = in.value("type", "Constant") == "Random" ?
			ParticleValueType::Random : ParticleValueType::Constant;
		value.constant = in.value("constant", value.constant);
		value.min = in.value("min", value.min);
		value.max = in.value("max", value.max);
	}

	// Vector3はToJson/FromJsonの形式で変換する
	template <>
	inline void to_json(nlohmann::json& out, const ParticleValue<Vector3>& value) {

		out["type"] = value.type == ParticleValueType::Random ? "Random" : "Constant";
		out["constant"] = value.constant.ToJson();
		out["min"] = value.min.ToJson();
		out["max"] = value.max.ToJson();
	}

	template <>
	inline void from_json(const nlohmann::json& in, ParticleValue<Vector3>& value) {

		if (!in.is_object()) {
			return;
		}
		value.type = in.value("type", "Constant") == "Random" ?
			ParticleValueType::Random : ParticleValueType::Constant;
		if (const auto it = in.find("constant"); it != in.end()) { value.constant = Vector3::FromJson(*it); }
		if (const auto it = in.find("min"); it != in.end()) { value.min = Vector3::FromJson(*it); }
		if (const auto it = in.find("max"); it != in.end()) { value.max = Vector3::FromJson(*it); }
	}
} // Engine
