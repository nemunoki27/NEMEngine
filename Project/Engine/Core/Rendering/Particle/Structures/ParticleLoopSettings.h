#pragma once

//============================================================================
//	include
//============================================================================

// c++
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
		float LoopedT(float rawT) const;
	};

	// json変換
	void to_json(nlohmann::json& out, const ParticleLoopSettings& loop);

	void from_json(const nlohmann::json& in, ParticleLoopSettings& loop);
} // Engine
