#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionDetection.h>
#include <vector>

namespace Engine { class ECSWorld; }

namespace Engine::ClothCollisionQuery {

	struct Collider {
		CollisionShapeInstance shape;
		uint32_t typeMask;
	};

	std::vector<Collider> Collect(ECSWorld& world);
	void Select(std::span<const Collider> candidates, uint32_t mask, std::vector<CollisionShapeInstance>& shapes);
	// 布の頂点を指定した厚みだけ立体表面から離す
	void Project(std::span<const CollisionShapeInstance> colliders, Vector3& worldPosition, float thickness);
}
