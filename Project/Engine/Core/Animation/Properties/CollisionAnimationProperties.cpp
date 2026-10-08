#include "BuiltinAnimationPropertyGroups.h"

//============================================================================
//	include
//============================================================================
#include "BuiltinAnimationPropertyUtility.h"
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>

using namespace Engine::AnimationPropertyUtility;

namespace {

	// 対象の衝突形状が存在するか確認する
	bool HasCollisionShape(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return world.HasComponent<Engine::CollisionComponent>(entity);
	}

	// 衝突形状の値を取得する
	template <typename Value, Value Engine::CollisionShape::* Member>
	bool GetCollisionShapeMember(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::AnimationPropertyValue& out) {

		if (Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity)) {
			out = collision->shape.*Member;
			return true;
		}
		return false;
	}

	// 衝突形状を変更して通知する
	template <typename Value, Value Engine::CollisionShape::* Member>
	bool SetCollisionShapeMember(
		Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::AnimationPropertyValue& value) {

		Value typed{};
		if (!ReadVariant(value, typed)) {
			return false;
		}
		if (Engine::CollisionComponent* collision = world.TryGetComponent<Engine::CollisionComponent>(entity)) {
			collision->shape.*Member = typed;
			world.MarkComponentModified<Engine::CollisionComponent>(entity);
			return true;
		}
		return false;
	}

	// 衝突形状の設定を登録する
	void RegisterCollisionShapeProperties(Engine::AnimationPropertyRegistry& registry) {

		Register(registry, "Collision", "shape.offset", "Collision.shape.offset", Engine::AnimationValueType::Vector3,
			HasCollisionShape, GetCollisionShapeMember<Engine::Vector3, &Engine::CollisionShape::offset>,
			SetCollisionShapeMember<Engine::Vector3, &Engine::CollisionShape::offset>);
		Register(registry, "Collision", "shape.rotationDegrees", "Collision.shape.rotationDegrees",
			Engine::AnimationValueType::Vector3, HasCollisionShape,
			GetCollisionShapeMember<Engine::Vector3, &Engine::CollisionShape::rotationDegrees>,
			SetCollisionShapeMember<Engine::Vector3, &Engine::CollisionShape::rotationDegrees>);
		Register(registry, "Collision", "shape.radius", "Collision.shape.radius", Engine::AnimationValueType::Float,
			HasCollisionShape, GetCollisionShapeMember<float, &Engine::CollisionShape::radius>,
			SetCollisionShapeMember<float, &Engine::CollisionShape::radius>);
		Register(registry, "Collision", "shape.halfSize2D", "Collision.shape.halfSize2D", Engine::AnimationValueType::Vector2,
			HasCollisionShape, GetCollisionShapeMember<Engine::Vector2, &Engine::CollisionShape::halfSize2D>,
			SetCollisionShapeMember<Engine::Vector2, &Engine::CollisionShape::halfSize2D>);
		Register(registry, "Collision", "shape.halfExtents3D", "Collision.shape.halfExtents3D",
			Engine::AnimationValueType::Vector3, HasCollisionShape,
			GetCollisionShapeMember<Engine::Vector3, &Engine::CollisionShape::halfExtents3D>,
			SetCollisionShapeMember<Engine::Vector3, &Engine::CollisionShape::halfExtents3D>);
		Register(registry, "Collision", "shape.capsuleHeight", "Collision.shape.capsuleHeight",
			Engine::AnimationValueType::Float, HasCollisionShape,
			GetCollisionShapeMember<float, &Engine::CollisionShape::capsuleHeight>,
			SetCollisionShapeMember<float, &Engine::CollisionShape::capsuleHeight>);
		Register(registry, "Collision", "shape.capsuleSize2D", "Collision.shape.capsuleSize2D",
			Engine::AnimationValueType::Vector2, HasCollisionShape,
			GetCollisionShapeMember<Engine::Vector2, &Engine::CollisionShape::capsuleSize2D>,
			SetCollisionShapeMember<Engine::Vector2, &Engine::CollisionShape::capsuleSize2D>);
	}
} // namespace

// Collisionの編集値を登録する
void Engine::RegisterCollisionAnimationProperties(AnimationPropertyRegistry& registry) {

	RegisterCollisionShapeProperties(registry);
}
