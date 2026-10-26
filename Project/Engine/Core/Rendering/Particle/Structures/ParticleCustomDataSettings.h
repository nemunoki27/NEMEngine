#pragma once

//============================================================================
//	include
//============================================================================
#include "ParticleMaterialStructures.h"

namespace Engine {

	// 描画時に粒子の寿命から評価する任意の数値
	struct ParticleCustomDataSettings {

		bool enabled = false;
		ParticleMaterialAnimatedParameter customData1{.componentCount = 4};
		ParticleMaterialAnimatedParameter customData2{.componentCount = 4};
	};

	// 任意数値を保存する
	void to_json(nlohmann::json& out, const ParticleCustomDataSettings& value);
	// 保存した任意数値を読む
	void from_json(const nlohmann::json& in, ParticleCustomDataSettings& value);
	// 発生番号から再現できる乱数を作る
	float ParticleStableRandom(uint64_t birthID);
}
