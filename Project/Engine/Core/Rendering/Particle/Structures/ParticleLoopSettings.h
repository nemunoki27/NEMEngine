#pragma once

//============================================================================
//	include
//============================================================================

// c++
#include <algorithm>
#include <cmath>
#include <cstdint>

// json
#include <json.hpp>

namespace Engine {

	//============================================================================
	//	ParticleLoopSettings
	//	進行度のループ設定、モジュールの補間で共用する
	//============================================================================
	// ループの種類
	enum class ParticleLoopType :
		uint8_t {

		Repeat,
		PingPong,
	};

	struct ParticleLoopSettings {

		// ループ回数
		int32_t loopCount = 1;
		// ループの種類
		ParticleLoopType type = ParticleLoopType::Repeat;

		// ループを適用した進行度を取得する
		float LoopedT(float rawT) const {

			if (loopCount <= 1) {
				return std::clamp(rawT, 0.0f, 1.0f);
			}
			float t = rawT * static_cast<float>(loopCount);
			if (type == ParticleLoopType::PingPong) {

				t = std::fmod(t, 2.0f);
				if (1.0f < t) {
					t = 2.0f - t;
				}
			} else {
				t = std::fmod(t, 1.0f);
			}
			return t;
		}
	};

	// json変換
	inline void to_json(nlohmann::json& out, const ParticleLoopSettings& loop) {

		out["loopCount"] = loop.loopCount;
		out["type"] = loop.type == ParticleLoopType::PingPong ? "PingPong" : "Repeat";
	}

	inline void from_json(const nlohmann::json& in, ParticleLoopSettings& loop) {

		if (!in.is_object()) {
			return;
		}
		loop.loopCount = in.value("loopCount", loop.loopCount);
		loop.type = in.value("type", "Repeat") == "PingPong" ?
			ParticleLoopType::PingPong : ParticleLoopType::Repeat;
	}
} // Engine
