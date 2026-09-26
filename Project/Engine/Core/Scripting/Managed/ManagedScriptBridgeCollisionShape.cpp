#include "ManagedScriptRuntime.h"
#include "ManagedScriptUtility.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/ECS/World/ECSWorld.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>

// c++
#include <cstring>

namespace Engine {

	namespace {

		Engine::CollisionShape* ResolveCollisionShape(
			Engine::ECSWorld& world, const Engine::Entity& entity) {

			if (!world.IsAlive(entity)) {
				return nullptr;
			}
			Engine::CollisionComponent* collision = world.TryGetComponentForBinding<Engine::CollisionComponent>(entity);
			return collision ? &collision->shape : nullptr;
		}
	}

	int32_t ManagedScriptRuntime::CollisionGetShapePropertyCallback(ManagedNativeEntity entity,
		int32_t propertyID, void* out, int32_t size) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const CollisionShape* shape = ResolveCollisionShape(*world, ResolveEntity(entity));
		if (!shape || !out) {
			return 0;
		}

		// propertyIDはC#側のCollisionShapeRefと対応する
		switch (propertyID) {
		case 0: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = static_cast<int32_t>(shape->type); return 1;
		case 1: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->enabled ? 1 : 0; return 1;
		case 2: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->isTrigger ? 1 : 0; return 1;
		case 3: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->useTransformRotation ? 1 : 0; return 1;
		case 4: if (size < 4) { return 0; } *reinterpret_cast<int32_t*>(out) = shape->rotatedQuad ? 1 : 0; return 1;
		case 5: if (size < 12) { return 0; } std::memcpy(out, &shape->offset, 12); return 1;
		case 6: if (size < 12) { return 0; } std::memcpy(out, &shape->rotationDegrees, 12); return 1;
		case 7: if (size < 4) { return 0; } std::memcpy(out, &shape->radius, 4); return 1;
		case 8: if (size < 8) { return 0; } std::memcpy(out, &shape->halfSize2D, 8); return 1;
		case 9: if (size < 12) { return 0; } std::memcpy(out, &shape->halfExtents3D, 12); return 1;
		case 10: if (size < 4) { return 0; } std::memcpy(out, &shape->capsuleHeight, 4); return 1;
		case 11: if (size < 8) { return 0; } std::memcpy(out, &shape->capsuleSize2D, 8); return 1;
		case 12:
			if (size < 4) { return 0; }
			*reinterpret_cast<int32_t*>(out) = static_cast<int32_t>(shape->capsuleAxis);
			return 1;
		}
		return 0;
	}

	int32_t ManagedScriptRuntime::CollisionSetShapePropertyCallback(ManagedNativeEntity entity,
		int32_t propertyID, const void* value, int32_t size) {

		ECSWorld* world = ResolveWorld(entity);
		if (!world) {
			return 0;
		}
		const Entity resolved = ResolveEntity(entity);
		CollisionShape* shape = ResolveCollisionShape(*world, resolved);
		if (!shape || !value) {
			return 0;
		}

		switch (propertyID) {
		case 0:
			if (size < 4) { return 0; }
			shape->type = static_cast<ColliderShapeType>(
				*reinterpret_cast<const int32_t*>(value));
			break;
		case 1: if (size < 4) { return 0; } shape->enabled = *reinterpret_cast<const int32_t*>(value) != 0; break;
		case 2: if (size < 4) { return 0; } shape->isTrigger = *reinterpret_cast<const int32_t*>(value) != 0; break;
		case 3:
			if (size < 4) { return 0; }
			shape->useTransformRotation =
				*reinterpret_cast<const int32_t*>(value) != 0;
			break;
		case 4: if (size < 4) { return 0; } shape->rotatedQuad = *reinterpret_cast<const int32_t*>(value) != 0; break;
		case 5: if (size < 12) { return 0; } std::memcpy(&shape->offset, value, 12); break;
		case 6: if (size < 12) { return 0; } std::memcpy(&shape->rotationDegrees, value, 12); break;
		case 7: if (size < 4) { return 0; } std::memcpy(&shape->radius, value, 4); break;
		case 8: if (size < 8) { return 0; } std::memcpy(&shape->halfSize2D, value, 8); break;
		case 9: if (size < 12) { return 0; } std::memcpy(&shape->halfExtents3D, value, 12); break;
		case 10: if (size < 4) { return 0; } std::memcpy(&shape->capsuleHeight, value, 4); break;
		case 11: if (size < 8) { return 0; } std::memcpy(&shape->capsuleSize2D, value, 8); break;
		case 12:
			if (size < 4) { return 0; }
			shape->capsuleAxis = static_cast<CapsuleAxis>(
				*reinterpret_cast<const int32_t*>(value));
			break;
		default:
			return 0;
		}
		world->MarkComponentModified<CollisionComponent>(resolved);
		return 1;
	}
}
