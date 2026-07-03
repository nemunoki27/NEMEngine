#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionDetection.h>

namespace Engine {

	//============================================================================
	//	CollisionRaycast structures
	//============================================================================
	// レイ、directionは正規化して使う
	struct Ray {

		Vector3 origin = Vector3::AnyInit(0.0f);
		Vector3 direction = Vector3(0.0f, 0.0f, 1.0f);
	};

	// レイキャストのヒット結果
	struct RaycastHit3D {

		// ヒットしたEntity
		Entity entity = Entity::Null();

		// ワールド空間のヒット点と法線
		Vector3 point = Vector3::AnyInit(0.0f);
		Vector3 normal = Vector3(0.0f, 1.0f, 0.0f);
		// originからの距離
		float distance = 0.0f;

		// CollisionComponent内の形状index、FillMeshヒット時は-1
		int32_t shapeIndex = -1;
		// FillMeshヒット時の三角形index、形状ヒット時は-1
		int32_t triangleIndex = -1;
		// Trigger形状へのヒットか
		bool trigger = false;
	};

	//============================================================================
	//	CollisionRaycast class
	//	レイと衝突形状の交差判定、レイ始点が形状内部の場合は距離0でヒット扱いにする
	//============================================================================
	class CollisionRaycast {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CollisionRaycast() = default;
		~CollisionRaycast() = default;

		// 球との交差
		static bool RayVsSphere(const Ray& ray, const Vector3& center, float radius,
			float maxDistance, float& outDistance, Vector3& outNormal);
		// OBBとの交差、AABBはワールド軸のOBBとして扱う
		static bool RayVsOBB(const Ray& ray, const CollisionShapeInstance& box,
			float maxDistance, float& outDistance, Vector3& outNormal);
		// 三角形との交差、両面判定で法線はレイへ向いた側を返す
		static bool RayVsTriangle(const Ray& ray, const Vector3& v0, const Vector3& v1, const Vector3& v2,
			float maxDistance, float& outDistance, Vector3& outNormal);
		// ワールド化済み形状との交差、2D形状は対象外でfalse
		static bool RayVsShape(const Ray& ray, const CollisionShapeInstance& shape,
			float maxDistance, float& outDistance, Vector3& outNormal);
	};
} // Engine
