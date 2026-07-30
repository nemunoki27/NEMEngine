#include "WorldManager.h"

//============================================================================
//	WorldManager classMethods
//============================================================================
Engine::WorldManager::WorldManager() :
	editWorld_(ECSWorldKind::Authoring) {
}

void Engine::WorldManager::CreatePlayWorld() {

	// Play中はBakerが生成した小さいRuntime Componentだけを更新対象にする
	playWorld_ = std::make_unique<ECSWorld>(ECSWorldKind::Runtime);
}

void Engine::WorldManager::DestroyPlayWorld() {

	playWorld_.reset();
}
