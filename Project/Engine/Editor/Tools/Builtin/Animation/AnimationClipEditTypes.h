#pragma once

#include <cstdint>

namespace Engine {

	enum class AnimationClipEditDimension :
		uint8_t {

		Auto,
		Mode2D,
		Mode3D,
	};

	enum class AnimationClipDetectedDimension :
		uint8_t {

		Unknown,
		Mode2D,
		Mode3D,
		Mixed,
	};

}
