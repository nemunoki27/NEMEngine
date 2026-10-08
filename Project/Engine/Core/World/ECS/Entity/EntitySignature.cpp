#include "EntitySignature.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Algorithm/HashUtility.h>

//============================================================================
//	EntitySignature classMethods
//============================================================================
bool Engine::operator==(const Engine::EntitySignature& signatureA, const Engine::EntitySignature& signatureB) {

	return signatureA.words == signatureB.words;
}

void Engine::EntitySignature::Set(uint32_t typeID) {

	const uint32_t w = typeID / 64;
	const uint32_t b = typeID % 64;
	words[w] |= (1ull << b);
}

void Engine::EntitySignature::Reset(uint32_t typeID) {

	const uint32_t w = typeID / 64;
	const uint32_t b = typeID % 64;
	words[w] &= ~(1ull << b);
}

bool Engine::EntitySignature::Test(uint32_t typeID) const {

	const uint32_t w = typeID / 64;
	const uint32_t b = typeID % 64;
	return (words[w] & (1ull << b)) != 0;
}

bool Engine::EntitySignature::Contains(const EntitySignature& rhs) const {

	for (uint32_t i = 0; i < kWordCount; ++i) {
		if ((words[i] & rhs.words[i]) != rhs.words[i]) {
			return false;
		}
	}
	return true;
}

size_t Engine::EntitySignatureHash::operator()(const EntitySignature& signature) const noexcept {

	size_t hash = 1469598103934665603ull;
	for (const auto& word : signature.words) {

		hash = Algorithm::MixHash(hash, static_cast<size_t>(word));
	}
	return hash;
}