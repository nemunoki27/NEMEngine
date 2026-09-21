#include "ParticleLoopSettings.h"

//============================================================================
//	include
//============================================================================
#include <algorithm>
#include <cmath>

//============================================================================
//	ParticleLoopSettings classMethods
//============================================================================

namespace Engine {

	float ParticleLoopSettings::LoopedT(float rawT) const {

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

	void to_json(nlohmann::json& out, const ParticleLoopSettings& loop) {

		out["loopCount"] = loop.loopCount;
		out["type"] = loop.type == ParticleLoopType::PingPong ? "PingPong" : "Repeat";
	}

	void from_json(const nlohmann::json& in, ParticleLoopSettings& loop) {

		if (!in.is_object()) {
			return;
		}
		loop.loopCount = in.value("loopCount", loop.loopCount);
		loop.type = in.value("type", "Repeat") == "PingPong" ?
			ParticleLoopType::PingPong : ParticleLoopType::Repeat;
	}
}
