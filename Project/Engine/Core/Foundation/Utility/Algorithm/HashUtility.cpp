#include "HashUtility.h"

//============================================================================
//	include
//============================================================================

namespace Engine::Algorithm {

	void HashCombine(uint64_t& hash, uint64_t value) {

		hash ^= value;
		hash *= 1099511628211ull;
	}
}
