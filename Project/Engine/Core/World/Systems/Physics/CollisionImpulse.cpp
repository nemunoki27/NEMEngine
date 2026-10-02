#include "CollisionImpulse.h"

// c++
#include <algorithm>

namespace {

	constexpr float kMaxAngularSpeed = 30.0f;
	constexpr float kRollingResist = 0.2f;

	float CalculateInverseInertia(float inverseMass,
		const Engine::CollisionShapeInstance& shape, bool is2D) {

		const float radiusSquared = shape.type == Engine::ColliderShapeType::Sphere3D ||
			shape.type == Engine::ColliderShapeType::Circle2D ? shape.radius * shape.radius :
			(is2D ? (shape.halfExtents.x * shape.halfExtents.x +
				shape.halfExtents.y * shape.halfExtents.y) :
				(shape.halfExtents.x * shape.halfExtents.x +
					shape.halfExtents.y * shape.halfExtents.y +
					shape.halfExtents.z * shape.halfExtents.z));
		return inverseMass / (std::max)(radiusSquared, 0.0001f);
	}
}

namespace Engine::CollisionImpulse {

	void ResolveContactPair3D(RigidbodyComponent& bodyA, RigidbodyComponent& bodyB,
		const Vector3& normalA, const Vector3& leverA, const Vector3& leverB,
		const CollisionShapeInstance& shapeA, const CollisionShapeInstance& shapeB) {

		const float inverseMassA = 1.0f / (std::max)(bodyA.mass, 0.0001f);
		const float inverseMassB = 1.0f / (std::max)(bodyB.mass, 0.0001f);
		const float inverseInertiaA = CalculateInverseInertia(inverseMassA, shapeA, false);
		const float inverseInertiaB = CalculateInverseInertia(inverseMassB, shapeB, false);

		// 両剛体の接触点速度から相対速度を求める
		auto velocityA = [&]() {
			return bodyA.linearVelocity + Vector3::Cross(bodyA.angularVelocity, leverA);
			};
		auto velocityB = [&]() {
			return bodyB.linearVelocity + Vector3::Cross(bodyB.angularVelocity, leverB);
			};
		Vector3 relativeVelocity = velocityA() - velocityB();
		const float normalSpeed = Vector3::Dot(relativeVelocity, normalA);
		if (normalSpeed >= 0.0f) {
			return;
		}

		// 両側の質量と慣性を含めて法線インパルスを配分する
		const Vector3 leverCrossNormalA = Vector3::Cross(leverA, normalA);
		const Vector3 leverCrossNormalB = Vector3::Cross(leverB, normalA);
		const float denominator = inverseMassA + inverseMassB +
			inverseInertiaA * Vector3::Dot(leverCrossNormalA, leverCrossNormalA) +
			inverseInertiaB * Vector3::Dot(leverCrossNormalB, leverCrossNormalB);
		if (denominator <= 0.000001f) {
			return;
		}
		const float restitution = (std::min)(bodyA.restitution, bodyB.restitution);
		const float normalImpulseValue = -(1.0f + restitution) * normalSpeed / denominator;
		const Vector3 normalImpulse = normalA * normalImpulseValue;
		bodyA.linearVelocity += normalImpulse * inverseMassA;
		bodyB.linearVelocity -= normalImpulse * inverseMassB;
		bodyA.angularVelocity += Vector3::Cross(leverA, normalImpulse) * inverseInertiaA;
		bodyB.angularVelocity -= Vector3::Cross(leverB, normalImpulse) * inverseInertiaB;

		// 接線インパルスを両剛体へ反対向きに適用する
		relativeVelocity = velocityA() - velocityB();
		const Vector3 tangentVelocity = relativeVelocity -
			normalA * Vector3::Dot(relativeVelocity, normalA);
		const float tangentSpeed = tangentVelocity.Length();
		if (tangentSpeed > 0.00001f) {

			const Vector3 tangent = tangentVelocity * (1.0f / tangentSpeed);
			const Vector3 leverCrossTangentA = Vector3::Cross(leverA, tangent);
			const Vector3 leverCrossTangentB = Vector3::Cross(leverB, tangent);
			const float tangentDenominator = inverseMassA + inverseMassB +
				inverseInertiaA * Vector3::Dot(leverCrossTangentA, leverCrossTangentA) +
				inverseInertiaB * Vector3::Dot(leverCrossTangentB, leverCrossTangentB);
			const float friction = std::sqrt((std::max)(bodyA.friction, 0.0f) *
				(std::max)(bodyB.friction, 0.0f));
			const float tangentImpulseValue = std::clamp(
				-Vector3::Dot(relativeVelocity, tangent) / tangentDenominator,
				-friction * normalImpulseValue, friction * normalImpulseValue);
			const Vector3 tangentImpulse = tangent * tangentImpulseValue;
			bodyA.linearVelocity += tangentImpulse * inverseMassA;
			bodyB.linearVelocity -= tangentImpulse * inverseMassB;
			bodyA.angularVelocity += Vector3::Cross(leverA, tangentImpulse) * inverseInertiaA;
			bodyB.angularVelocity -= Vector3::Cross(leverB, tangentImpulse) * inverseInertiaB;
		}
	}

	void ResolveContactPair2D(Rigidbody2DComponent& bodyA, Rigidbody2DComponent& bodyB,
		const Vector2& normalA, const Vector2& leverA, const Vector2& leverB,
		const CollisionShapeInstance& shapeA, const CollisionShapeInstance& shapeB) {

		const float inverseMassA = 1.0f / (std::max)(bodyA.mass, 0.0001f);
		const float inverseMassB = 1.0f / (std::max)(bodyB.mass, 0.0001f);
		const float inverseInertiaA = CalculateInverseInertia(inverseMassA, shapeA, true);
		const float inverseInertiaB = CalculateInverseInertia(inverseMassB, shapeB, true);
		auto velocityA = [&]() {
			return bodyA.linearVelocity + Vector2(
				-bodyA.angularVelocity * leverA.y, bodyA.angularVelocity * leverA.x);
			};
		auto velocityB = [&]() {
			return bodyB.linearVelocity + Vector2(
				-bodyB.angularVelocity * leverB.y, bodyB.angularVelocity * leverB.x);
			};
		Vector2 relativeVelocity = velocityA() - velocityB();
		const float normalSpeed = Vector2::Dot(relativeVelocity, normalA);
		if (normalSpeed >= 0.0f) {
			return;
		}

		// 2D外積を使って両剛体の慣性を法線拘束へ加える
		const float leverCrossNormalA = leverA.x * normalA.y - leverA.y * normalA.x;
		const float leverCrossNormalB = leverB.x * normalA.y - leverB.y * normalA.x;
		const float denominator = inverseMassA + inverseMassB +
			inverseInertiaA * leverCrossNormalA * leverCrossNormalA +
			inverseInertiaB * leverCrossNormalB * leverCrossNormalB;
		if (denominator <= 0.000001f) {
			return;
		}
		const float restitution = (std::min)(bodyA.restitution, bodyB.restitution);
		const float normalImpulseValue = -(1.0f + restitution) * normalSpeed / denominator;
		bodyA.linearVelocity += normalA * (normalImpulseValue * inverseMassA);
		bodyB.linearVelocity -= normalA * (normalImpulseValue * inverseMassB);
		bodyA.angularVelocity += leverCrossNormalA * normalImpulseValue * inverseInertiaA;
		bodyB.angularVelocity -= leverCrossNormalB * normalImpulseValue * inverseInertiaB;

		relativeVelocity = velocityA() - velocityB();
		const Vector2 tangentVelocity = relativeVelocity -
			normalA * Vector2::Dot(relativeVelocity, normalA);
		const float tangentSpeed = tangentVelocity.Length();
		if (tangentSpeed > 0.00001f) {

			const Vector2 tangent = tangentVelocity * (1.0f / tangentSpeed);
			const float leverCrossTangentA = leverA.x * tangent.y - leverA.y * tangent.x;
			const float leverCrossTangentB = leverB.x * tangent.y - leverB.y * tangent.x;
			const float tangentDenominator = inverseMassA + inverseMassB +
				inverseInertiaA * leverCrossTangentA * leverCrossTangentA +
				inverseInertiaB * leverCrossTangentB * leverCrossTangentB;
			const float friction = std::sqrt((std::max)(bodyA.friction, 0.0f) *
				(std::max)(bodyB.friction, 0.0f));
			const float tangentImpulseValue = std::clamp(
				-Vector2::Dot(relativeVelocity, tangent) / tangentDenominator,
				-friction * normalImpulseValue, friction * normalImpulseValue);
			bodyA.linearVelocity += tangent * (tangentImpulseValue * inverseMassA);
			bodyB.linearVelocity -= tangent * (tangentImpulseValue * inverseMassB);
			bodyA.angularVelocity += leverCrossTangentA * tangentImpulseValue * inverseInertiaA;
			bodyB.angularVelocity -= leverCrossTangentB * tangentImpulseValue * inverseInertiaB;
		}
	}

	void ResolveContact3D(Engine::RigidbodyComponent& body, const Engine::Vector3& normal,
		const Engine::Vector3& lever, const Engine::CollisionShapeInstance& shape) {

		const float invMass = 1.0f / (body.mass > 0.0f ? body.mass : 1.0f);
		const float invInertia = CalculateInverseInertia(invMass, shape, false);

		// 接触点の速度がめり込む向きでなければ何もしない
		Engine::Vector3 contactVel = body.linearVelocity + Engine::Vector3::Cross(body.angularVelocity, lever);
		const float vn = Engine::Vector3::Dot(contactVel, normal);
		if (vn >= 0.0f) {
			return;
		}

		// 法線インパルス
		const Engine::Vector3 rCrossN = Engine::Vector3::Cross(lever, normal);
		const float denom = invMass + invInertia * Engine::Vector3::Dot(rCrossN, rCrossN);
		const float jn = -(1.0f + body.restitution) * vn / denom;
		const Engine::Vector3 normalImpulse = normal * jn;
		body.linearVelocity += normalImpulse * invMass;
		body.angularVelocity += Engine::Vector3::Cross(lever, normalImpulse) * invInertia;

		// 接線方向の摩擦インパルス、クーロン摩擦で法線インパルスに比例して制限する
		contactVel = body.linearVelocity + Engine::Vector3::Cross(body.angularVelocity, lever);
		const Engine::Vector3 tangentVel = contactVel - normal * Engine::Vector3::Dot(contactVel, normal);
		const float tangentSpeed = tangentVel.Length();
		if (tangentSpeed > 1e-5f) {

			const Engine::Vector3 tangent = tangentVel * (1.0f / tangentSpeed);
			const Engine::Vector3 rCrossT = Engine::Vector3::Cross(lever, tangent);
			const float denomT = invMass + invInertia * Engine::Vector3::Dot(rCrossT, rCrossT);
			const float maxFriction = body.friction * jn;
			const float jt = std::clamp(-Engine::Vector3::Dot(contactVel, tangent) / denomT, -maxFriction, maxFriction);
			const Engine::Vector3 frictionImpulse = tangent * jt;
			body.linearVelocity += frictionImpulse * invMass;
			body.angularVelocity += Engine::Vector3::Cross(lever, frictionImpulse) * invInertia;
		}

		// ころがり抵抗、滑らない転がりはクーロン摩擦が効かないので回転と接線速度を直接抜いて静止させる
		const float resist = std::clamp(body.friction * kRollingResist, 0.0f, 1.0f);
		body.angularVelocity -= body.angularVelocity * resist;
		const Engine::Vector3 slideVel = body.linearVelocity - normal * Engine::Vector3::Dot(body.linearVelocity, normal);
		body.linearVelocity -= slideVel * resist;

		const float angSpeed = body.angularVelocity.Length();
		if (angSpeed > kMaxAngularSpeed) {
			body.angularVelocity *= kMaxAngularSpeed / angSpeed;
		}
	}

	void ResolveContact2D(Engine::Rigidbody2DComponent& body, const Engine::Vector2& normal,
		const Engine::Vector2& lever, const Engine::CollisionShapeInstance& shape) {

		const float invMass = 1.0f / (body.mass > 0.0f ? body.mass : 1.0f);
		const float invInertia = CalculateInverseInertia(invMass, shape, true);

		// 角速度の接触点への寄与は ω × r = ω * (-r.y, r.x)
		auto contactVelocity = [&]() {
			return body.linearVelocity + Engine::Vector2(-body.angularVelocity * lever.y, body.angularVelocity * lever.x);
			};
		Engine::Vector2 contactVel = contactVelocity();
		const float vn = Engine::Vector2::Dot(contactVel, normal);
		if (vn >= 0.0f) {
			return;
		}

		// 2Dの外積はスカラー r.x*v.y - r.y*v.x
		const float rCrossN = lever.x * normal.y - lever.y * normal.x;
		const float denom = invMass + invInertia * rCrossN * rCrossN;
		const float jn = -(1.0f + body.restitution) * vn / denom;
		body.linearVelocity += normal * (jn * invMass);
		body.angularVelocity += rCrossN * jn * invInertia;

		contactVel = contactVelocity();
		const Engine::Vector2 tangentVel = contactVel - normal * Engine::Vector2::Dot(contactVel, normal);
		const float tangentSpeed = tangentVel.Length();
		if (tangentSpeed > 1e-5f) {

			const Engine::Vector2 tangent = tangentVel * (1.0f / tangentSpeed);
			const float rCrossT = lever.x * tangent.y - lever.y * tangent.x;
			const float denomT = invMass + invInertia * rCrossT * rCrossT;
			const float maxFriction = body.friction * jn;
			const float jt = std::clamp(-Engine::Vector2::Dot(contactVel, tangent) / denomT, -maxFriction, maxFriction);
			body.linearVelocity += tangent * (jt * invMass);
			body.angularVelocity += rCrossT * jt * invInertia;
		}

		// ころがり抵抗、滑らない転がりはクーロン摩擦が効かないので回転と接線速度を直接抜いて静止させる
		const float resist = std::clamp(body.friction * kRollingResist, 0.0f, 1.0f);
		body.angularVelocity -= body.angularVelocity * resist;
		const Engine::Vector2 slideVel = body.linearVelocity - normal * Engine::Vector2::Dot(body.linearVelocity, normal);
		body.linearVelocity -= slideVel * resist;

		body.angularVelocity = std::clamp(body.angularVelocity, -kMaxAngularSpeed, kMaxAngularSpeed);
	}
}
