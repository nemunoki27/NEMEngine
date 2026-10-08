#include "ToolTypes.h"

//============================================================================
//	ToolTypes classMethods
//============================================================================

namespace Engine {

	ToolFlags operator|(ToolFlags lhs, ToolFlags rhs) {

		return static_cast<ToolFlags>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
	}

	bool HasToolFlag(ToolFlags flags, ToolFlags target) {

		return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(target)) != 0;
	}
}
