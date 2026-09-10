#include "CollisionSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
#include <Engine/Core/Physics/Collision/CollisionShapeUtility.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Systems/Behavior/BehaviorSystem.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <algorithm>
#include <cmath>

//============================================================================
//	CollisionSystem classMethods
//============================================================================

namespace {

	// 相手側Entityへ渡すためのContactを作成する
	Engine::CollisionContact MakeSwappedContact(const Engine::CollisionContact& contact) {

		Engine::CollisionContact swapped = contact;
		std::swap(swapped.self, swapped.other);
		std::swap(swapped.selfShapeIndex, swapped.otherShapeIndex);
		swapped.normal = -contact.normal;
		return swapped;
	}

	// Rigidbodyの固定軸を移動量へ反映する
	Engine::Vector3 ApplyTranslationConstraints(Engine::ECSWorld& world,
		const Engine::Entity& entity, Engine::Vector3 value) {

		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(entity)) {

			if (body->freezePositionX) { value.x = 0.0f; }
			if (body->freezePositionY) { value.y = 0.0f; }
			if (body->freezePositionZ) { value.z = 0.0f; }
		}
		if (const auto* body = world.TryGetComponent<Engine::Rigidbody2DComponent>(entity)) {

			if (body->freezePositionX) { value.x = 0.0f; }
			if (body->freezePositionY) { value.y = 0.0f; }
		}
		return value;
	}

	// Transform変更をworldMatrixへ即時反映する
	void UpdateTransformWorldMatrix(Engine::ECSWorld& world,
		const Engine::Entity& entity, Engine::TransformComponent& transform) {

		Engine::MarkTransformSubtreeDirty(world, entity);

		Engine::Matrix4x4 parentWorld = Engine::Matrix4x4::Identity();
		if (world.HasComponent<Engine::HierarchyComponent>(entity)) {

			const auto& hierarchy = world.GetComponent<Engine::HierarchyComponent>(entity);
			if (world.IsAlive(hierarchy.parent) && world.HasComponent<Engine::TransformComponent>(hierarchy.parent)) {
				parentWorld = world.GetComponent<Engine::TransformComponent>(hierarchy.parent).worldMatrix;
			}
		}
		const Engine::Matrix4x4 localMatrix = Engine::Matrix4x4::MakeAffineMatrix(
			transform.localScale, transform.localRotation, transform.localPos);
		transform.worldMatrix = localMatrix * parentWorld;
	}

	// Entityを移動し、Transform更新対象にする
	void MoveEntity(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Vector3& delta) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}
		const Engine::Vector3 constrainedDelta = ApplyTranslationConstraints(world, entity, delta);
		if (constrainedDelta.Length() <= 0.000001f) {
			return;
		}
		auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
		transform.localPos += constrainedDelta;
		UpdateTransformWorldMatrix(world, entity, transform);
	}

	// Dynamic剛体を持つか
	bool IsDynamicRigidbody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (world.HasComponent<Engine::RigidbodyComponent>(entity)) {
			return world.GetComponent<Engine::RigidbodyComponent>(entity).bodyType == Engine::RigidbodyType::Dynamic;
		}
		if (world.HasComponent<Engine::Rigidbody2DComponent>(entity)) {
			return world.GetComponent<Engine::Rigidbody2DComponent>(entity).bodyType == Engine::RigidbodyType::Dynamic;
		}
		return false;
	}

	// 2Dか3Dの剛体を持つか
	bool HasRigidbody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return world.HasComponent<Engine::RigidbodyComponent>(entity) ||
			world.HasComponent<Engine::Rigidbody2DComponent>(entity);
	}

	// 剛体なしとStatic剛体だけを継ぎ目補正の地形にする
	bool IsBoxSurfaceBody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(entity)) {
			return body->bodyType == Engine::RigidbodyType::Static;
		}
		return !world.HasComponent<Engine::Rigidbody2DComponent>(entity);
	}

	// 慣性半径の近似、回転の効きを決める
	constexpr float kInertiaRadius = 0.5f;
	// 数値暴走を防ぐ角速度の上限 rad/s
	constexpr float kMaxAngularSpeed = 30.0f;
	// 接地中のころがり抵抗の強さ、frictionに掛けて1フレームあたりの減衰率にする
	constexpr float kRollingResist = 0.2f;
	// 支持面として扱う上向き法線の下限
	constexpr float kSupportNormalMin = 0.5f;
	// 自身と支持面が安定して接しているとみなす軸一致率
	constexpr float kSupportAlignmentMin = 0.98f;
	// 支持範囲の境界で数値誤差による転倒を防ぐ余白
	constexpr float kSupportEdgeTolerance = 0.001f;
	// 微小なめり込みを許容して接地中の押し戻し往復を防ぐ
	constexpr float kPenetrationSlop = 0.001f;
	// 3Dは急な姿勢変化を抑え、2Dはタイル床上で押し戻しを重複させないよう接触面まで補正する
	constexpr float kPenetrationCorrectionRate3D = 0.8f;
	constexpr float kPenetrationCorrectionRate2D = 1.0f;
	// 接地後に停止扱いにする角速度
	constexpr float kRestingAngularSpeed = 0.001f;
	// 拘束方向へ押し戻せるかを判定する下限
	constexpr float kTranslationResponseEpsilon = 0.000001f;

	// 2D形状か
	bool IsShape2D(const Engine::CollisionShapeInstance* shape) {

		return shape && Engine::IsCollisionShape2D(shape->type);
	}

	// 3D剛体の固定軸へ衝突速度を残さない
	void ApplyVelocityConstraints(Engine::RigidbodyComponent& body) {

		if (body.freezePositionX) { body.linearVelocity.x = 0.0f; }
		if (body.freezePositionY) { body.linearVelocity.y = 0.0f; }
		if (body.freezePositionZ) { body.linearVelocity.z = 0.0f; }
	}

	// 2D剛体の固定軸へ衝突速度を残さない
	void ApplyVelocityConstraints(Engine::Rigidbody2DComponent& body) {

		if (body.freezePositionX) { body.linearVelocity.x = 0.0f; }
		if (body.freezePositionY) { body.linearVelocity.y = 0.0f; }
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

	// 接触点速度に基づくインパルス法で速度と角速度を解く、3D
	// 接触点の速度が角速度を含むため接地で収束し、ころがり抵抗で回転と移動が静止する
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

	// 接触点速度に基づくインパルス法で速度と角速度を解く、2DはZ軸まわりのスカラー角速度
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

	// 3DのBox形状か
	bool IsBoxShape3D(const Engine::CollisionShapeInstance* shape) {

		return shape && (shape->type == Engine::ColliderShapeType::AABB3D ||
			shape->type == Engine::ColliderShapeType::OBB3D);
	}

	// 指定法線と最も一致するBox軸を返す
	uint32_t FindClosestBoxAxis(const Engine::CollisionShapeInstance& shape,
		const Engine::Vector3& normal, float& outAlignment) {

		uint32_t result = 0;
		outAlignment = std::fabs(Engine::Vector3::Dot(shape.axes[0], normal));
		for (uint32_t i = 1; i < 3; ++i) {

			const float alignment = std::fabs(Engine::Vector3::Dot(shape.axes[i], normal));
			if (outAlignment < alignment) {
				result = i;
				outAlignment = alignment;
			}
		}
		return result;
	}

	// Transform回転の軸から指定法線に最も近い軸を返す
	uint32_t FindClosestTransformAxis(const Engine::TransformComponent& transform,
		const Engine::Vector3& normal, float& outAlignment, Engine::Vector3& outAxis) {

		const Engine::Matrix4x4 rotation =
			Engine::Quaternion::MakeRotateMatrix(transform.localRotation);
		const Engine::Vector3 axes[3] = {
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(
				Engine::Vector3(1.0f, 0.0f, 0.0f), rotation), Engine::Vector3(1.0f, 0.0f, 0.0f)),
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(
				Engine::Vector3(0.0f, 1.0f, 0.0f), rotation), Engine::Vector3(0.0f, 1.0f, 0.0f)),
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(
				Engine::Vector3(0.0f, 0.0f, 1.0f), rotation), Engine::Vector3(0.0f, 0.0f, 1.0f)),
		};

		uint32_t result = 0;
		outAlignment = std::fabs(Engine::Vector3::Dot(axes[0], normal));
		for (uint32_t i = 1; i < 3; ++i) {

			const float alignment = std::fabs(Engine::Vector3::Dot(axes[i], normal));
			if (outAlignment < alignment) {
				result = i;
				outAlignment = alignment;
			}
		}
		outAxis = axes[result];
		return result;
	}

	// 支持面へ最も近いBox面を正確に揃える
	void SettleSupportedRotation(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::Vector3& normal, const Engine::CollisionShapeInstance* selfShape) {

		if (!selfShape ||
			(!IsBoxShape3D(selfShape) && selfShape->type != Engine::ColliderShapeType::Quad2D) ||
			!world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}

		auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
		float alignment = 0.0f;
		Engine::Vector3 faceNormal{};
		if (selfShape->type == Engine::ColliderShapeType::AABB3D) {
			FindClosestTransformAxis(transform, normal, alignment, faceNormal);
		} else {
			const uint32_t axisIndex = FindClosestBoxAxis(*selfShape, normal, alignment);
			faceNormal = selfShape->axes[axisIndex];
		}
		if (alignment < kSupportAlignmentMin) {
			return;
		}
		if (Engine::Vector3::Dot(faceNormal, normal) < 0.0f) {
			faceNormal = -faceNormal;
		}

		Engine::Vector3 correctionAxis = Engine::Vector3::Cross(faceNormal, normal);
		const float correctionSin = correctionAxis.Length();
		if (correctionSin <= 0.0000001f) {
			return;
		}
		correctionAxis *= 1.0f / correctionSin;
		const float correctionCos = std::clamp(
			Engine::Vector3::Dot(faceNormal, normal), -1.0f, 1.0f);
		const float correctionAngle = std::atan2(correctionSin, correctionCos);
		Engine::Quaternion correction =
			Engine::Quaternion::MakeAxisAngle(correctionAxis, correctionAngle);

		// 追加形状回転より外側で求めた補正をTransformのローカル回転へ変換する
		if (selfShape->type != Engine::ColliderShapeType::AABB3D) {
			const auto* collision = world.TryGetComponent<Engine::CollisionComponent>(entity);
			if (!collision || !collision->shape.useTransformRotation) {
				return;
			}
			const Engine::Quaternion shapeRotation = Engine::Quaternion::FromEulerDegrees(
				collision->shape.rotationDegrees);
			correction = Engine::Quaternion::Inverse(shapeRotation) *
				correction * shapeRotation;
		}

		transform.localRotation = Engine::Quaternion::Normalize(
			correction * transform.localRotation);
		if (std::fabs(transform.localRotation.x) <= 0.000001f) { transform.localRotation.x = 0.0f; }
		if (std::fabs(transform.localRotation.y) <= 0.000001f) { transform.localRotation.y = 0.0f; }
		if (std::fabs(transform.localRotation.z) <= 0.000001f) { transform.localRotation.z = 0.0f; }
		if (std::fabs(transform.localRotation.w - 1.0f) <= 0.000001f) { transform.localRotation.w = 1.0f; }
		transform.localRotation = Engine::Quaternion::Normalize(transform.localRotation);
		UpdateTransformWorldMatrix(world, entity, transform);
	}

	// 重心が支持面内にあり、自身の面が支持面へ揃っているか
	bool IsCenterSupported3D(const Engine::TransformComponent& transform,
		const Engine::Vector3& centerOfMass,
		const Engine::Vector3& normal, const Engine::CollisionShapeInstance* selfShape,
		const Engine::CollisionShapeInstance* supportShape) {

		if (normal.y < kSupportNormalMin ||
			!IsBoxShape3D(selfShape) || !IsBoxShape3D(supportShape)) {
			return false;
		}

		float supportAlignment = 0.0f;
		const uint32_t supportAxis = FindClosestBoxAxis(*supportShape, normal, supportAlignment);
		float selfAlignment = 0.0f;
		if (selfShape->type == Engine::ColliderShapeType::AABB3D) {
			Engine::Vector3 selfAxis{};
			FindClosestTransformAxis(transform, normal, selfAlignment, selfAxis);
		} else {
			FindClosestBoxAxis(*selfShape, normal, selfAlignment);
		}
		if (supportAlignment < kSupportAlignmentMin || selfAlignment < kSupportAlignmentMin) {
			return false;
		}

		const Engine::Vector3 local = centerOfMass - supportShape->center;
		for (uint32_t i = 0; i < 3; ++i) {

			if (i == supportAxis) {
				continue;
			}
			const float halfExtent = i == 0 ? supportShape->halfExtents.x :
				(i == 1 ? supportShape->halfExtents.y : supportShape->halfExtents.z);
			if (halfExtent + kSupportEdgeTolerance <
				std::fabs(Engine::Vector3::Dot(local, supportShape->axes[i]))) {
				return false;
			}
		}
		return true;
	}

	// 2Dで重心が支持面内にあり、自身の辺が支持面へ揃っているか
	bool IsCenterSupported2D(const Engine::Vector3& centerOfMass,
		const Engine::Vector3& normal, const Engine::CollisionShapeInstance* selfShape,
		const Engine::CollisionShapeInstance* supportShape) {

		if (normal.y < kSupportNormalMin || !selfShape || !supportShape ||
			selfShape->type != Engine::ColliderShapeType::Quad2D ||
			supportShape->type != Engine::ColliderShapeType::Quad2D) {
			return false;
		}

		float supportAlignment = 0.0f;
		const uint32_t supportAxis = FindClosestBoxAxis(*supportShape, normal, supportAlignment);
		float selfAlignment = 0.0f;
		FindClosestBoxAxis(*selfShape, normal, selfAlignment);
		if (1 < supportAxis || supportAlignment < kSupportAlignmentMin ||
			selfAlignment < kSupportAlignmentMin) {
			return false;
		}

		const uint32_t tangentAxis = supportAxis == 0 ? 1 : 0;
		const float halfExtent = tangentAxis == 0 ?
			supportShape->halfExtents.x : supportShape->halfExtents.y;
		const Engine::Vector3 local = centerOfMass - supportShape->center;
		return std::fabs(Engine::Vector3::Dot(local, supportShape->axes[tangentAxis])) <=
			halfExtent + kSupportEdgeTolerance;
	}

	// 支持面に対する傾きだけを止め、法線まわりの回転は摩擦で減衰させる
	void StabilizeSupportedRotation(Engine::RigidbodyComponent& body,
		const Engine::Vector3& normal) {

		const float spin = Engine::Vector3::Dot(body.angularVelocity, normal);
		body.angularVelocity = normal * spin *
			std::clamp(1.0f - body.friction, 0.0f, 1.0f);
		if (body.angularVelocity.Length() <= kRestingAngularSpeed) {
			body.angularVelocity = Engine::Vector3::AnyInit(0.0f);
		}
	}

	// 接触面で速度を反発と摩擦で更新する、allowToppleがONなら接触点まわりの回転も解く
	void ResolveContactVelocity(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::Vector3& pushOutDir, const Engine::Vector3& contactPoint,
		const Engine::CollisionShapeInstance* selfShape,
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
			const bool supported = body.allowTopple &&
				IsCenterSupported3D(transform, com, pushOutDir, selfShape, supportShape);
			if (body.allowTopple && !supported) {

				ResolveContact3D(body, pushOutDir, lever);
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
			const bool supported = body.allowTopple && !body.freezeRotation &&
				IsCenterSupported2D(com, pushOutDir, selfShape, supportShape);
			if (body.allowTopple && !body.freezeRotation && !supported) {
				ResolveContact2D(body, normal2D, Engine::Vector2(lever.x, lever.y));
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
}

void Engine::CollisionSystem::OnWorldExit([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	previousContacts_.clear();
}

void Engine::CollisionSystem::RebuildRuntimeShape(
	[[maybe_unused]] ECSWorld& world, CollisionRuntimeEntity& runtime) const {

	runtime.hasShape = false;
	if (!runtime.collision || !runtime.transform) {
		return;
	}
	if (!runtime.collision->shape.enabled) {
		return;
	}
	runtime.shape = CollisionShapeUtility::BuildShapeInstance(
		runtime.entity, runtime.collision->shape, 0, *runtime.transform);
	runtime.hasShape = true;
}

void Engine::CollisionSystem::FixedUpdate(ECSWorld& world, SystemContext& context) {

	// 押し戻しとコールバックはPlay中の固定ステップだけで処理する
	if (context.mode != WorldMode::Play) {
		return;
	}
	UpdateCollisions(world, context, true);
}

void Engine::CollisionSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// Play中の判定はFixedUpdateで完了している
	if (context.mode == WorldMode::Play) {
		return;
	}
	UpdateCollisions(world, context, false);
}

void Engine::CollisionSystem::UpdateCollisions(ECSWorld& world, SystemContext& context, bool applyResponse) {

	// 実行時状態は設定データと分離し、衝突したEntityだけ後で立てる
	world.ForEach<CollisionRuntimeStateComponent>([](
		Entity, CollisionRuntimeStateComponent& state) {
		state.colliding = false;
		});

	CollisionSettings& settings = CollisionSettings::GetInstance();
	settings.EnsureLoaded();

	std::vector<CollisionRuntimeEntity> entities{};
	world.ForEach<CollisionComponent, TransformComponent>([&](
		Entity entity, CollisionComponent& collision, TransformComponent& transform) {

			if (!collision.enabled || !IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// Componentが持つ単一形状を判定用形状へ変換する
			CollisionRuntimeEntity runtime{};
			runtime.entity = entity;
			runtime.collision = &collision;
			runtime.state = world.TryGetComponent<CollisionRuntimeStateComponent>(entity);
			runtime.transform = &transform;
			runtime.dynamicBody = IsDynamicRigidbody(world, entity);
			runtime.surfaceBox = collision.enablePushback && IsBoxSurfaceBody(world, entity) &&
				(collision.shape.type == ColliderShapeType::AABB3D || collision.shape.type == ColliderShapeType::OBB3D) &&
				!collision.shape.isTrigger;
			RebuildRuntimeShape(world, runtime);
			if (runtime.hasShape) {
				entities.emplace_back(std::move(runtime));
			}
		});

	// 地形の面情報は固定ステップごとに構築し、接触ペア間で共有する
	const bool hasDynamicBox = std::any_of(entities.begin(), entities.end(), [](const CollisionRuntimeEntity& runtime) {
		return runtime.dynamicBody && (runtime.shape.type == ColliderShapeType::AABB3D ||
			runtime.shape.type == ColliderShapeType::OBB3D) && !runtime.shape.trigger;
		});
	if (applyResponse && hasDynamicBox) {
		std::vector<CollisionBoxSurface> surfaces;
		surfaces.reserve(entities.size());
		for (const auto& runtime : entities) {
			surfaces.push_back({ runtime.surfaceBox ? &runtime.shape : nullptr, runtime.collision->typeMask, 0 });
		}
		BuildBoxInternalFaces(surfaces);
		for (size_t i = 0; i < entities.size(); ++i) {
			entities[i].internalFaces = surfaces[i].internalFaces;
		}
	}
	bool surfaceGeometryChanged = false;
	std::unordered_map<CollisionPairKey, CollisionContact, CollisionPairKeyHash> currentContacts{};
	for (uint32_t aIndex = 0; aIndex < static_cast<uint32_t>(entities.size()); ++aIndex) {
		for (uint32_t bIndex = aIndex + 1; bIndex < static_cast<uint32_t>(entities.size()); ++bIndex) {

			CollisionRuntimeEntity& a = entities[aIndex];
			CollisionRuntimeEntity& b = entities[bIndex];
			if (!settings.CanCollide(a.collision->typeMask, b.collision->typeMask)) {
				continue;
			}

			// 剛体なし同士の押し戻しが起きた後は古い隣接情報を使わない
			const uint8_t facesA = !surfaceGeometryChanged && b.dynamicBody ? a.internalFaces : 0;
			const uint8_t facesB = !surfaceGeometryChanged && a.dynamicBody ? b.internalFaces : 0;
			CollisionContact contact{};
			const bool colliding = (facesA | facesB) ?
				TestCollisionWithBoxInternalFaces(a.shape, b.shape, facesA, facesB, contact) :
				TestCollision(a.shape, b.shape, contact);
			if (!colliding) {
				continue;
			}

			const CollisionPairKey key = CollisionPairKey::Make(a.entity, b.entity);
			currentContacts[key] = contact;

			// 衝突中フラグを立てて形状描画を赤くする、トリガーの重なりも衝突として扱う
			if (a.state) {
				a.state->colliding = true;
			}
			if (b.state) {
				b.state->colliding = true;
			}

			// 押し戻しとEnter / Stayの分配は固定ステップのみ行う
			if (applyResponse) {
				const Vector3 centerA = a.shape.center;
				const Vector3 centerB = b.shape.center;
				ApplyPushback(world, a, b, contact);
				surfaceGeometryChanged |= (a.surfaceBox && (a.shape.center - centerA).Length() > 0.0f) ||
					(b.surfaceBox && (b.shape.center - centerB).Length() > 0.0f);
				if (previousContacts_.contains(key)) {
					DispatchCollisionStay(world, context, contact);
				} else {
					DispatchCollisionEnter(world, context, contact);
				}
			}
		}
	}

	// Edit中はコールバックも履歴も持たず、表示用フラグだけ更新して終える
	if (!applyResponse) {
		previousContacts_.clear();
		return;
	}

	// 前フレームにだけ存在した接触はExitとして扱う
	for (const auto& [key, contact] : previousContacts_) {
		if (currentContacts.contains(key)) {
			continue;
		}
		// 破棄済みEntityはExitで死んだハンドルをスクリプトへ渡さないよう対象外にする
		if (!world.IsAlive(contact.self) || !world.IsAlive(contact.other)) {
			continue;
		}
		DispatchCollisionExit(world, context, contact);
	}
	previousContacts_ = std::move(currentContacts);
}

void Engine::CollisionSystem::ApplyPushback(ECSWorld& world,
	CollisionRuntimeEntity& a, CollisionRuntimeEntity& b, const CollisionContact& contact) const {

	if (contact.trigger || !a.collision || !b.collision) {
		return;
	}

	// 剛体が絡む場合はDynamicだけ動かし、剛体なし側はUnity同様に不動の静的コライダー扱いにする
	// これがないと地面側もenablePushbackで押し戻され、剛体が地面ごと沈んで貫通する
	bool movableA;
	bool movableB;
	if (HasRigidbody(world, a.entity) || HasRigidbody(world, b.entity)) {

		movableA = IsDynamicRigidbody(world, a.entity);
		movableB = IsDynamicRigidbody(world, b.entity);
	} else {

		movableA = !a.collision->isStatic && a.collision->enablePushback;
		movableB = !b.collision->isStatic && b.collision->enablePushback;
	}
	if (!movableA && !movableB) {
		return;
	}

	const CollisionShapeInstance* shapeA = a.hasShape ? &a.shape : nullptr;
	const CollisionShapeInstance* shapeB = b.hasShape ? &b.shape : nullptr;
	const bool resolveAs2D = IsShape2D(shapeA) && IsShape2D(shapeB);

	// 固定軸を除いた法線成分で、めり込みを解消できる側へ押し戻し量を配分する
	const Vector3 responseDirectionA = movableA ?
		ApplyTranslationConstraints(world, a.entity, contact.normal) : Vector3::AnyInit(0.0f);
	const Vector3 responseDirectionB = movableB ?
		ApplyTranslationConstraints(world, b.entity, contact.normal) : Vector3::AnyInit(0.0f);
	const float responseA = (std::max)(Vector3::Dot(responseDirectionA, contact.normal), 0.0f);
	const float responseB = (std::max)(Vector3::Dot(responseDirectionB, contact.normal), 0.0f);
	const float responseSum = responseA + responseB;
	const float correctionDepth = (std::max)(contact.penetration - kPenetrationSlop, 0.0f);
	bool movedA = false;
	bool movedB = false;
	if (kTranslationResponseEpsilon < responseSum && 0.0f < correctionDepth) {

		const float correctionRate = resolveAs2D ?
			kPenetrationCorrectionRate2D : kPenetrationCorrectionRate3D;
		const float correctionScale =
			correctionDepth * correctionRate / responseSum;
		if (movableA) {
			MoveEntity(world, a.entity, -responseDirectionA * correctionScale);
			movedA = true;
		}
		if (movableB) {
			MoveEntity(world, b.entity, responseDirectionB * correctionScale);
			movedB = true;
		}
	}

	if (movableA) {
		ResolveContactVelocity(world, a.entity, -contact.normal,
			contact.point, shapeA, shapeB);
	}
	if (movableB) {
		ResolveContactVelocity(world, b.entity, contact.normal,
			contact.point, shapeB, shapeA);
	}

	// 隣接Colliderを続けて解く場合も、補正前の形状で二重に押し戻さない
	if (movedA) {
		RebuildRuntimeShape(world, a);
	}
	if (movedB) {
		RebuildRuntimeShape(world, b);
	}
}

void Engine::CollisionSystem::DispatchCollisionEnter(ECSWorld& world,
	SystemContext& context, const CollisionContact& contact) const {

	BehaviorSystem::DispatchCollisionEnter(world, context, contact);
	BehaviorSystem::DispatchCollisionEnter(world, context, MakeSwappedContact(contact));
}

void Engine::CollisionSystem::DispatchCollisionStay(ECSWorld& world,
	SystemContext& context, const CollisionContact& contact) const {

	BehaviorSystem::DispatchCollisionStay(world, context, contact);
	BehaviorSystem::DispatchCollisionStay(world, context, MakeSwappedContact(contact));
}

void Engine::CollisionSystem::DispatchCollisionExit(ECSWorld& world,
	SystemContext& context, const CollisionContact& contact) const {

	BehaviorSystem::DispatchCollisionExit(world, context, contact);
	BehaviorSystem::DispatchCollisionExit(world, context, MakeSwappedContact(contact));
}
