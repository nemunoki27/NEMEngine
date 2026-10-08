#include "AnimationChannelUtility.h"

//============================================================================
//	include
//============================================================================
// c++
#include <algorithm>

namespace Engine::AnimationChannelUtility {

	Engine::Color4 GetChannelColor(std::string_view name) {

		// 座標と色のチャンネルを同じ配色にする
		if (name == "X" || name == "R") {
			return Engine::Color4::Red();
		}
		if (name == "Y" || name == "G") {
			return Engine::Color4::Green();
		}
		if (name == "Z" || name == "B") {
			return Engine::Color4::Blue();
		}
		if (name == "W" || name == "A") {
			return Engine::Color4(0.85f, 0.85f, 0.85f, 1.0f);
		}
		if (name == "Axis") {
			return Engine::Color4(0.95f, 0.85f, 0.20f, 1.0f);
		}
		if (name == "Angle") {
			return Engine::Color4(0.25f, 0.65f, 1.0f, 1.0f);
		}
		return Engine::Color4::White();
	}

	size_t GetSharedKeyCount(std::span<const CurveChannel> channels) {

		if (channels.empty()) {
			return 0;
		}
		// 最も短いチャンネルへ共通の範囲を揃える
		size_t count = channels.front().keys.size();
		for (const CurveChannel& channel : channels.subspan(1)) {
			count = (std::min)(count, channel.keys.size());
		}
		return count;
	}
}
