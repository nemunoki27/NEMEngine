#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/ECS/Entity/Entity.h>
#include <Engine/MathLib/Vector2.h>
#include <Engine/MathLib/Vector3.h>

// c++
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

	//============================================================================
	//	constant
	//============================================================================

	// CollisionManagerで扱える衝突タイプ数
	constexpr uint32_t kMaxCollisionTypes = 32;

	//============================================================================
	//	ColliderShapeType enum class
	//	衝突形状の種類
	//============================================================================
	enum class ColliderShapeType :
		int32_t {

		Circle2D = 0,
		Quad2D,
		Sphere3D,
		AABB3D,
		OBB3D,
	};

	//============================================================================
	//	CollisionShape structure
	//	Entityに設定する衝突形状
	//============================================================================
	struct CollisionShape {

		// 判定形状
		ColliderShapeType type = ColliderShapeType::Sphere3D;

		// 有効状態
		bool enabled = true;
		// trueの場合は押し戻しを行わず、コールバックだけを発行する
		bool isTrigger = false;
		// Transformの回転を形状回転に含める
		bool useTransformRotation = true;
		// Quad2Dを回転矩形として扱う
		bool rotatedQuad = false;

		// Transformからの相対位置と追加回転
		Vector3 offset = Vector3::AnyInit(0.0f);
		Vector3 rotationDegrees = Vector3::AnyInit(0.0f);

		// Circle2D / Sphere3D用
		float radius = 0.5f;
		// Quad2D用
		Vector2 halfSize2D = Vector2::AnyInit(0.5f);
		// AABB3D / OBB3D用
		Vector3 halfExtents3D = Vector3::AnyInit(0.5f);
	};

	//============================================================================
	//	CollisionTypeDefinition structure
	//	CollisionManagerで作成する衝突タイプ
	//============================================================================
	struct CollisionTypeDefinition {

		std::string name = "Default";
		bool enabled = true;
	};

	//============================================================================
	//	CollisionContact structure
	//	OnCollisionに渡す衝突情報
	//============================================================================
	struct CollisionContact {

		// コールバックを受け取るEntityと相手Entity
		Entity self = Entity::Null();
		Entity other = Entity::Null();

		// selfからotherへ向かう押し戻し法線、代表接触点、めり込み量
		Vector3 normal = Vector3::AnyInit(0.0f);
		Vector3 point = Vector3::AnyInit(0.0f);
		float penetration = 0.0f;

		// 衝突した形状インデックス
		uint32_t selfShapeIndex = 0;
		uint32_t otherShapeIndex = 0;

		// どちらかの形状がTriggerならtrue
		bool trigger = false;
	};

	//============================================================================
	//	CollisionPairKey structure
	//	Entityの組み合わせを順序に依存せず扱うキー
	//============================================================================
	struct CollisionPairKey {

		uint32_t lowIndex = 0;
		uint32_t lowGeneration = 0;
		uint32_t highIndex = 0;
		uint32_t highGeneration = 0;

		// Entityの組み合わせからキーを作成する
		static CollisionPairKey Make(Entity entityA, Entity entityB);

		bool operator==(const CollisionPairKey& other) const noexcept;
	};

	//============================================================================
	//	CollisionPairKeyHash structure
	//============================================================================
	struct CollisionPairKeyHash {

		size_t operator()(const CollisionPairKey& key) const noexcept;
	};

	// ColliderShapeTypeを文字列へ変換する
	const char* ToString(ColliderShapeType type);
	// 文字列からColliderShapeTypeへ変換する
	ColliderShapeType ColliderShapeTypeFromString(const std::string& text, ColliderShapeType fallback = ColliderShapeType::Sphere3D);

	// 衝突タイプインデックスからビットを作成する
	uint32_t MakeCollisionTypeBit(uint32_t typeIndex);
	// マスクに指定衝突タイプが含まれているか
	bool HasCollisionType(uint32_t mask, uint32_t typeIndex);
}
