#include "WorldManager.h"

//============================================================================
//	WorldManager classMethods
//============================================================================

void Engine::WorldManager::CreatePlayWorld() {

	playWorld_ = std::make_unique<ECSWorld>();
}

void Engine::WorldManager::DestroyPlayWorld() {

	playWorld_.reset();
}