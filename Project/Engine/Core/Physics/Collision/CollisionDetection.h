#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionTypes.h>

// c++
#include <cstddef>
#include <span>

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
		// カプセル中心線の両端、球とBoxではcenterと同値
		Vector3 segmentStart = Vector3::AnyInit(0.0f);
		Vector3 segmentEnd = Vector3::AnyInit(0.0f);
	};

	// 隣接する地形の内部面を固定ステップ中だけ保持する
	struct CollisionBoxSurface {

		const CollisionShapeInstance* shape = nullptr;
		uint32_t typeMask = 0;
		uint8_t internalFaces = 0;
	};

	// 地形の隣接面を構築し、広域候補の比較回数を返す
	size_t BuildBoxInternalFaces(std::span<CollisionBoxSurface> surfaces);
	// 内部面の接触だけを近傍の外側面へ補正し、通常の接触は保持する
	bool TestCollisionWithBoxInternalFaces(const CollisionShapeInstance& a, const CollisionShapeInstance& b,
		uint8_t facesA, uint8_t facesB, CollisionContact& outContact);

	// 2D用の衝突形状か
	bool IsCollisionShape2D(ColliderShapeType type);
	// 3D用の衝突形状か
	bool IsCollisionShape3D(ColliderShapeType type);
	// 2つの形状の衝突判定を行う
	bool TestCollision(const CollisionShapeInstance& a, const CollisionShapeInstance& b, CollisionContact& outContact);
}
