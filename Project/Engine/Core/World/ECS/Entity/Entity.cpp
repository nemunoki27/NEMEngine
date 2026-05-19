#include "Entity.h"

//============================================================================
//	Entity classMethods
//============================================================================

bool Engine::operator==(const Entity& entityA, const Entity& entityB) noexcept {

	return entityA.index == entityB.index && entityA.generation == entityB.generation;
}

bool Engine::operator!=(const Entity& entityA, const Entity& entityB) noexcept {

	return !(entityA == entityB);
}