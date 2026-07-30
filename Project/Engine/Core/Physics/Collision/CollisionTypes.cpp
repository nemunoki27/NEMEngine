#include "CollisionTypes.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Foundation/Utility/Enum/EnumAdapter.h>

// c++
#include <functional>

namespace {

	// Entityのindexとgenerationを比較用にまとめる
	uint64_t PackEntity(Engine::Entity entity) {

		return (static_cast<uint64_t>(entity.index) << 32) | entity.generation;
	}
}

//============================================================================
//	CollisionShape classMethods
//============================================================================
void Engine::from_json(const nlohmann::json& in, CollisionShape& shape) {

	if (!in.is_object()) {
		return;
	}
	shape.type = EnumAdapter<ColliderShapeType>::FromString(
		in.value("type", "Sphere3D")).value_or(ColliderShapeType::Sphere3D);
	shape.enabled = in.value("enabled", shape.enabled);
	shape.isTrigger = in.value("isTrigger", shape.isTrigger);
	shape.useTransformRotation =
		in.value("useTransformRotation", shape.useTransformRotation);
	shape.rotatedQuad = in.value("rotatedQuad", shape.rotatedQuad);
	if (in.contains("offset")) {
		shape.offset = Vector3::FromJson(in["offset"]);
	}
	if (in.contains("rotationDegrees")) {
		shape.rotationDegrees = Vector3::FromJson(in["rotationDegrees"]);
	}
	shape.radius = in.value("radius", shape.radius);
	if (in.contains("halfSize2D")) {
		shape.halfSize2D = Vector2::FromJson(in["halfSize2D"]);
	}
	if (in.contains("halfExtents3D")) {
		shape.halfExtents3D = Vector3::FromJson(in["halfExtents3D"]);
	}
}

void Engine::to_json(nlohmann::json& out, const CollisionShape& shape) {

	out = nlohmann::json::object();
	out["type"] = EnumAdapter<ColliderShapeType>::ToString(shape.type);
	out["enabled"] = shape.enabled;
	out["isTrigger"] = shape.isTrigger;
	out["useTransformRotation"] = shape.useTransformRotation;
	out["rotatedQuad"] = shape.rotatedQuad;
	out["offset"] = shape.offset.ToJson();
	out["rotationDegrees"] = shape.rotationDegrees.ToJson();
	out["radius"] = shape.radius;
	out["halfSize2D"] = shape.halfSize2D.ToJson();
	out["halfExtents3D"] = shape.halfExtents3D.ToJson();
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
uint32_t Engine::MakeCollisionTypeBit(uint32_t typeIndex) {

	if (typeIndex >= kMaxCollisionTypes) {
		return 0;
	}
	return 1u << typeIndex;
}

bool Engine::HasCollisionType(uint32_t mask, uint32_t typeIndex) {

	return (mask & MakeCollisionTypeBit(typeIndex)) != 0;
}
