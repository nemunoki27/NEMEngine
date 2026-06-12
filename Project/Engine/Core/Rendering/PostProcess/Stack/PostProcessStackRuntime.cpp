#include "PostProcessStackRuntime.h"

//============================================================================
//	PostProcessStackRuntime classMethods
//============================================================================
bool Engine::PostProcessStackRuntime::HasEnabledPasses() const {

	for (const auto& pass : passes) {
		if (pass.enabled) {
			return true;
		}
	}
	return false;
}
