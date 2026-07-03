#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionDetection.h>

namespace Engine {

	// front
	struct TransformComponent;

	//============================================================================
	//	CollisionShapeUtility class
	//	衝突形状をTransformを反映したワールド空間のインスタンスへ変換する
	//============================================================================
	class CollisionShapeUtility {
	public:
		//========================================================================
		//	public Methods
		//========================================================================

		CollisionShapeUtility() = default;
		~CollisionShapeUtility() = default;

		// 形状をワールド空間の判定用インスタンスにする
		static CollisionShapeInstance BuildShapeInstance(const Entity& entity,
			const CollisionShape& shape, uint32_t shapeIndex, const TransformComponent& transform);
	};
} // Engine
