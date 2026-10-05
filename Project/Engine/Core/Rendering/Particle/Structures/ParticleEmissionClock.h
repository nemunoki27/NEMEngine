#pragma once

//============================================================================
//	include
//============================================================================
#include <cstdint>

namespace Engine {

	// 発生間隔の超過時間を保持して発生回数を返す
	uint32_t AdvanceParticleEmissionClock(float& timer, float interval, float deltaTime);
}
