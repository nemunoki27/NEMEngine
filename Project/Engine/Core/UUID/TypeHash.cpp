#include "TypeHash.h"

//============================================================================
//	TypeHash classMethods
//============================================================================

uint32_t Engine::EntityToTypeHash(const std::string_view& string) {

	uint32_t hash = 2166136261u;
	for (char c : string) {
		hash ^= static_cast<uint8_t>(c);
		hash *= 16777619u;
	}
	return hash;
}