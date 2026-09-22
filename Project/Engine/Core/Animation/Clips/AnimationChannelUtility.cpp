#include "AnimationChannelUtility.h"

namespace Engine::AnimationChannelUtility {

	Engine::Color4 GetChannelColor(std::string_view name) {

		// X/Y/Z/WとR/G/B/Aを同じ色規則にして、Vector/Colorで見た目を揃える
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
}
