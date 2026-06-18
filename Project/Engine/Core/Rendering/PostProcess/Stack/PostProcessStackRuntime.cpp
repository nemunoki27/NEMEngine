#include "PostProcessStackRuntime.h"

//============================================================================
//	PostProcessStackRuntime classMethods
//============================================================================
bool Engine::PostProcessStackRuntime::HasEnabledPassesForAnchor(PostProcessAnchor anchor) const {

	for (const auto& pass : passes) {
		if (pass.enabled && pass.anchor == anchor) {
			return true;
		}
	}
	return false;
}
