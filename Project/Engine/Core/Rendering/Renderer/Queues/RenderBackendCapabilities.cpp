#include "RenderBackendCapabilities.h"

//============================================================================
//	RenderBackendCapabilities classMethods
//============================================================================

namespace Engine::RenderBackendCapabilities {

	bool SupportsOutlineMask(uint32_t backendID) {

		return backendID == RenderBackendID::Mesh ||
			backendID == RenderBackendID::Primitive ||
			backendID == RenderBackendID::Sprite;
	}
}
