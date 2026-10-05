#include "CollisionContactResponse.h"

//============================================================================
//	include
//============================================================================
#include "CollisionImpulse.h"
#include "CollisionSupport.h"
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/ECS/World/ECSWorld.h>

// c++
#include <algorithm>

using namespace Engine::CollisionImpulse;
using namespace Engine::CollisionSupport;

namespace Engine::CollisionContactResponse {

	// 3D剛体の固定軸へ衝突速度を残さない
	void ApplyVelocityConstraints(Engine::RigidbodyComponent& body) {

		if (body.freezePositionX) {
			body.linearVelocity.x = 0.0f;
		}
		if (body.freezePositionY) {
			body.linearVelocity.y = 0.0f;
		}
		if (body.freezePositionZ) {
			body.linearVelocity.z = 0.0f;
		}
	}

	// 2D剛体の固定軸へ衝突速度を残さない
	void ApplyVelocityConstraints(Engine::Rigidbody2DComponent& body) {

		if (body.freezePositionX) {
			body.linearVelocity.x = 0.0f;
		}
		if (body.freezePositionY) {
			body.linearVelocity.y = 0.0f;
		}
	}

	// 回転を使わないときの線形のみの反発と摩擦
	template <typename Vec>
	void ResolveLinearOnly(Vec& velocity, const Vec& normal, float restitution, float friction) {

		const float into = Vec::Dot(velocity, normal);
		if (into >= 0.0f) {
			return;
		}
		velocity -= normal * (into * (1.0f + restitution));
		const Vec tangent = velocity - normal * Vec::Dot(velocity, normal);
		velocity -= tangent * friction;
	}

	// 接触面で速度を反発と摩擦で更新する、allowToppleがONなら接触点まわりの回転も解く
	void ResolveContactVelocity(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Vector3& pushOutDir,
		const Engine::Vector3& contactPoint, const Engine::CollisionShapeInstance* selfShape,
		const Engine::CollisionShapeInstance* supportShape) {

		if (!world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}
		// 接触点から重心へのてこの腕
		const auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
		const Engine::Vector3 com = transform.worldMatrix.GetTranslationValue();
		const Engine::Vector3 lever = contactPoint - com;

		if (world.HasComponent<Engine::RigidbodyComponent>(entity)) {

			auto& body = world.GetComponent<Engine::RigidbodyComponent>(entity);
			if (body.bodyType != Engine::RigidbodyType::Dynamic) {
				return;
			}
			const bool supported = body.allowTopple && IsCenterSupported3D(transform, com, pushOutDir, selfShape, supportShape);
			if (body.allowTopple && !supported) {

				ResolveContact3D(body, pushOutDir, lever, *selfShape);
			} else {
				ResolveLinearOnly(body.linearVelocity, pushOutDir, body.restitution, body.friction);
			}
			if (supported) {
				StabilizeSupportedRotation(body, pushOutDir);
				SettleSupportedRotation(world, entity, pushOutDir, selfShape);
			}
			ApplyVelocityConstraints(body);
		}
		if (world.HasComponent<Engine::Rigidbody2DComponent>(entity)) {

			auto& body = world.GetComponent<Engine::Rigidbody2DComponent>(entity);
			if (body.bodyType != Engine::RigidbodyType::Dynamic) {
				return;
			}
			const Engine::Vector2 normal2D = Engine::Vector2(pushOutDir.x, pushOutDir.y);
			const bool supported =
				body.allowTopple && !body.freezeRotation && IsCenterSupported2D(com, pushOutDir, selfShape, supportShape);
			if (body.allowTopple && !body.freezeRotation && !supported) {
				ResolveContact2D(body, normal2D, Engine::Vector2(lever.x, lever.y), *selfShape);
			} else {
				ResolveLinearOnly(body.linearVelocity, normal2D, body.restitution, body.friction);
			}
			if (supported) {
				body.angularVelocity = 0.0f;
				SettleSupportedRotation(world, entity, pushOutDir, selfShape);
			}
			ApplyVelocityConstraints(body);
		}
	}

	bool ResolvePairContactVelocity(Engine::ECSWorld& world, const Engine::Entity& entityA, const Engine::Entity& entityB,
		const Engine::Vector3& normalA, const Engine::Vector3& contactPoint, const Engine::CollisionShapeInstance* shapeA,
		const Engine::CollisionShapeInstance* shapeB) {

		if (!shapeA || !shapeB) {
			return false;
		}
		const auto* transformA = world.TryGetComponent<Engine::TransformComponent>(entityA);
		const auto* transformB = world.TryGetComponent<Engine::TransformComponent>(entityB);
		if (!transformA || !transformB) {
			return false;
		}
		const Engine::Vector3 leverA = contactPoint - transformA->worldMatrix.GetTranslationValue();
		const Engine::Vector3 leverB = contactPoint - transformB->worldMatrix.GetTranslationValue();
		if (auto* bodyA = world.TryGetComponent<Engine::RigidbodyComponent>(entityA)) {

			auto* bodyB = world.TryGetComponent<Engine::RigidbodyComponent>(entityB);
			if (!bodyB || bodyA->bodyType != Engine::RigidbodyType::Dynamic ||
				bodyB->bodyType != Engine::RigidbodyType::Dynamic) {
				return false;
			}
			Engine::CollisionImpulse::ResolveContactPair3D(*bodyA, *bodyB, normalA, leverA, leverB, *shapeA, *shapeB);
			ApplyVelocityConstraints(*bodyA);
			ApplyVelocityConstraints(*bodyB);
			return true;
		}
		if (auto* bodyA = world.TryGetComponent<Engine::Rigidbody2DComponent>(entityA)) {

			auto* bodyB = world.TryGetComponent<Engine::Rigidbody2DComponent>(entityB);
			if (!bodyB || bodyA->bodyType != Engine::RigidbodyType::Dynamic ||
				bodyB->bodyType != Engine::RigidbodyType::Dynamic) {
				return false;
			}
			Engine::CollisionImpulse::ResolveContactPair2D(*bodyA, *bodyB, Engine::Vector2(normalA.x, normalA.y),
				Engine::Vector2(leverA.x, leverA.y), Engine::Vector2(leverB.x, leverB.y), *shapeA, *shapeB);
			ApplyVelocityConstraints(*bodyA);
			ApplyVelocityConstraints(*bodyB);
			return true;
		}
		return false;
	}
}
