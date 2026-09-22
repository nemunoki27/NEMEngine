#include "RigidbodyIntegration.h"

// c++
#include <algorithm>
#include <cmath>

namespace {

	// 3DはY+が上、2Dスクリーン座標はY+が下
	const Engine::Vector3 kGravity3D = Engine::Vector3(0.0f, -9.81f, 0.0f);
	const Engine::Vector2 kGravity2D = Engine::Vector2(0.0f, 9.81f);

	// 力と重力を速度へ反映して減衰させる、2Dと3Dで共通
	template <typename Vec>
	void IntegrateVelocity(Vec& velocity, const Vec& force, const Vec& gravityStep, float mass, float damping, float dt) {

		velocity += force * (dt / mass);
		velocity += gravityStep;
		velocity *= std::clamp(1.0f - damping * dt, 0.0f, 1.0f);
	}
}

void Engine::RigidbodyIntegration::Integrate(RigidbodyComponent& body, TransformComponent& transform, float dt) {

	const float mass = body.mass > 0.0f ? body.mass : 1.0f;
	const Vector3 gravityStep = body.useGravity ?
		kGravity3D * (body.gravityScale * dt) : Vector3::AnyInit(0.0f);
	IntegrateVelocity(body.linearVelocity, body.accumulatedForce, gravityStep, mass, body.linearDamping, dt);

	// 拘束軸の速度を止める
	if (body.freezePositionX) { body.linearVelocity.x = 0.0f; }
	if (body.freezePositionY) { body.linearVelocity.y = 0.0f; }
	if (body.freezePositionZ) { body.linearVelocity.z = 0.0f; }

	// 位置を更新して蓄積力を消費する
	transform.localPos += body.linearVelocity * dt;
	body.accumulatedForce = Vector3::AnyInit(0.0f);

	// 蓄積トルクを角速度へ反映する、慣性は質量スカラで近似する
	body.angularVelocity += body.accumulatedTorque * (dt / mass);
	body.accumulatedTorque = Vector3::AnyInit(0.0f);

	// 角速度で姿勢を更新して減衰させる
	const float angSpeed = body.angularVelocity.Length();
	if (angSpeed > 1e-5f) {

		const Vector3 axis = Vector3::Normalize(body.angularVelocity);
		const Quaternion spin = Quaternion::MakeAxisAngle(axis, angSpeed * dt);
		transform.localRotation = Quaternion::Normalize(spin * transform.localRotation);
	}
	body.angularVelocity *= std::clamp(1.0f - body.angularDamping * dt, 0.0f, 1.0f);

}

void Engine::RigidbodyIntegration::Integrate(Rigidbody2DComponent& body, TransformComponent& transform, float dt) {

	const float mass = body.mass > 0.0f ? body.mass : 1.0f;
	const Vector2 gravityStep = body.useGravity ?
		kGravity2D * (body.gravityScale * dt) : Vector2::AnyInit(0.0f);
	IntegrateVelocity(body.linearVelocity, body.accumulatedForce, gravityStep, mass, body.linearDamping, dt);

	if (body.freezePositionX) { body.linearVelocity.x = 0.0f; }
	if (body.freezePositionY) { body.linearVelocity.y = 0.0f; }

	transform.localPos.x += body.linearVelocity.x * dt;
	transform.localPos.y += body.linearVelocity.y * dt;
	body.accumulatedForce = Vector2::AnyInit(0.0f);

	// 蓄積トルクをZ軸角速度へ反映する、慣性は質量スカラで近似する
	body.angularVelocity += body.accumulatedTorque * (dt / mass);
	body.accumulatedTorque = 0.0f;

	// Z軸まわりの角速度で姿勢を更新して減衰させる
	if (!body.freezeRotation && std::fabs(body.angularVelocity) > 1e-5f) {

		const Quaternion spin = Quaternion::MakeAxisAngle(Vector3(0.0f, 0.0f, 1.0f), body.angularVelocity * dt);
		transform.localRotation = Quaternion::Normalize(spin * transform.localRotation);
	}
	body.angularVelocity *= std::clamp(1.0f - body.angularDamping * dt, 0.0f, 1.0f);

}
