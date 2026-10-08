#include "WorldEntityKey.h"

//============================================================================
//	include
//============================================================================
// c++
#include <functional>

//============================================================================
//	WorldEntityKeyHash structMethods
//============================================================================
size_t Engine::WorldEntityKeyHash::operator()(const WorldEntityKey& key) const noexcept {

	// 別WorldとEntityの再利用を区別
	size_t hash = std::hash<const void*>{}(key.world);
	hash ^= std::hash<uint32_t>{}(key.entity.index) << 1;
	hash ^= std::hash<uint32_t>{}(key.entity.generation) << 2;
	return hash;
}
