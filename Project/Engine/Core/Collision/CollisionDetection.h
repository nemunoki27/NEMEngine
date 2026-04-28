#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Collision/CollisionTypes.h>

namespace Engine {

	//============================================================================
	//	CollisionShapeInstance structure
	//	判定用にTransformを反映した衝突形状
	//============================================================================
	struct CollisionShapeInstance {

		// 判定対象Entityと形状番号
		Entity entity = Entity::Null();
		uint32_t shapeIndex = 0;

		// 判定形状とTrigger状態
		ColliderShapeType type = ColliderShapeType::Sphere3D;
		bool trigger = false;

		// ワールド空間上の中心と向き
		Vector3 center = Vector3::AnyInit(0.0f);
		Vector3 axes[3] = {
			Vector3(1.0f, 0.0f, 0.0f),
			Vector3(0.0f, 1.0f, 0.0f),
			Vector3(0.0f, 0.0f, 1.0f),
		};

		// 判定サイズ
		Vector3 halfExtents = Vector3::AnyInit(0.5f);
		float radius = 0.5f;
	};

	// 2D用の衝突形状か
	bool IsCollisionShape2D(ColliderShapeType type);
	// 3D用の衝突形状か
	bool IsCollisionShape3D(ColliderShapeType type);
	// 2つの形状の衝突判定を行う
	bool TestCollision(const CollisionShapeInstance& a, const CollisionShapeInstance& b, CollisionContact& outContact);
}
