#include "BehaviorHandle.h"

//============================================================================
//	BehaviorHandle classMethods
//============================================================================

bool Engine::operator==(const BehaviorHandle& handleA, const BehaviorHandle& handleB) noexcept {

	return handleA.index == handleB.index && handleA.generation == handleB.generation;
}

bool Engine::operator!=(const BehaviorHandle& handleA, const BehaviorHandle& handleB) noexcept {

	return !(handleA == handleB);
}