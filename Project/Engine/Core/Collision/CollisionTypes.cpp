#include "CollisionTypes.h"

// c++
#include <functional>

namespace {

	// Entityのindexとgenerationを比較用にまとめる
	uint64_t PackEntity(Engine::Entity entity) {

		return (static_cast<uint64_t>(entity.index) << 32) | entity.generation;
	}
}

//============================================================================
//	CollisionPairKey classMethods
//============================================================================

Engine::CollisionPairKey Engine::CollisionPairKey::Make(Entity entityA, Entity entityB) {

	const uint64_t packedA = PackEntity(entityA);
	const uint64_t packedB = PackEntity(entityB);
	if (packedA <= packedB) {
		return { entityA.index, entityA.generation, entityB.index, entityB.generation };
	}
	return { entityB.index, entityB.generation, entityA.index, entityA.generation };
}

bool Engine::CollisionPairKey::operator==(const CollisionPairKey& other) const noexcept {

	return lowIndex == other.lowIndex &&
		lowGeneration == other.lowGeneration &&
		highIndex == other.highIndex &&
		highGeneration == other.highGeneration;
}

//============================================================================
//	CollisionPairKeyHash classMethods
//============================================================================

size_t Engine::CollisionPairKeyHash::operator()(const CollisionPairKey& key) const noexcept {

	size_t seed = std::hash<uint32_t>{}(key.lowIndex);
	auto mix = [&](uint32_t value) {
		seed ^= std::hash<size_t>{}(static_cast<size_t>(value) + 0x9e3779b9ull + (seed << 6) + (seed >> 2));
		};
	mix(key.lowGeneration);
	mix(key.highIndex);
	mix(key.highGeneration);
	return seed;
}

//============================================================================
//	Collision utility
//============================================================================

const char* Engine::ToString(ColliderShapeType type) {

	switch (type) {
	case ColliderShapeType::Circle2D:
		return "Circle2D";
	case ColliderShapeType::Quad2D:
		return "Quad2D";
	case ColliderShapeType::Sphere3D:
		return "Sphere3D";
	case ColliderShapeType::AABB3D:
		return "AABB3D";
	case ColliderShapeType::OBB3D:
		return "OBB3D";
	default:
		return "Sphere3D";
	}
}

Engine::ColliderShapeType Engine::ColliderShapeTypeFromString(const std::string& text, ColliderShapeType fallback) {

	if (text == "Circle2D") {
		return ColliderShapeType::Circle2D;
	}
	if (text == "Quad2D") {
		return ColliderShapeType::Quad2D;
	}
	if (text == "Sphere3D") {
		return ColliderShapeType::Sphere3D;
	}
	if (text == "AABB3D") {
		return ColliderShapeType::AABB3D;
	}
	if (text == "OBB3D") {
		return ColliderShapeType::OBB3D;
	}
	return fallback;
}

uint32_t Engine::MakeCollisionTypeBit(uint32_t typeIndex) {

	if (typeIndex >= kMaxCollisionTypes) {
		return 0;
	}
	return 1u << typeIndex;
}

bool Engine::HasCollisionType(uint32_t mask, uint32_t typeIndex) {

	return (mask & MakeCollisionTypeBit(typeIndex)) != 0;
}
