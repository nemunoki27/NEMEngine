#include "CollisionImpulse.h"

// c++
#include <algorithm>

namespace {

	constexpr float kInertiaRadius = 0.5f;
	constexpr float kMaxAngularSpeed = 30.0f;
	constexpr float kRollingResist = 0.2f;
}

namespace Engine::CollisionImpulse {

	void ResolveContact3D(Engine::RigidbodyComponent& body, const Engine::Vector3& normal,
		const Engine::Vector3& lever) {

		const float invMass = 1.0f / (body.mass > 0.0f ? body.mass : 1.0f);
		const float invInertia = invMass / (kInertiaRadius * kInertiaRadius);

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
		const Engine::Vector2& lever) {

		const float invMass = 1.0f / (body.mass > 0.0f ? body.mass : 1.0f);
		const float invInertia = invMass / (kInertiaRadius * kInertiaRadius);

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
