#include "SceneEntityKey.h"

//============================================================================
//	include
//============================================================================
// c++
#include <functional>

//============================================================================
//	SceneEntityKeyHash methods
//============================================================================
size_t Engine::SceneEntityKeyHash::operator()(const SceneEntityKey& key) const noexcept {

	// Sceneの実体と文書内IDを一組で混合する
	const size_t sceneHash = std::hash<UUID>{}(key.sceneInstanceID);
	const size_t localHash = std::hash<UUID>{}(key.localFileID);
	return sceneHash ^ (localHash + 0x9e3779b9 + (sceneHash << 6) + (sceneHash >> 2));
}
