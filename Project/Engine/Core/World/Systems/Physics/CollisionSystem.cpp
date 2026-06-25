#include "CollisionSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionSettings.h>
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

	// Vector3の各要素を絶対値にする
	Engine::Vector3 AbsVector(const Engine::Vector3& value) {

		return Engine::Vector3(std::fabs(value.x), std::fabs(value.y), std::fabs(value.z));
	}

	// 行列から指定基底方向の軸を取り出す
	Engine::Vector3 ExtractAxis(const Engine::Matrix4x4& matrix, const Engine::Vector3& basis) {

		return Engine::Vector3::TransferNormal(basis, matrix);
	}

	// 行列から指定基底方向のスケールを取り出す
	float ExtractScale(const Engine::Matrix4x4& matrix, const Engine::Vector3& basis) {

		const float length = ExtractAxis(matrix, basis).Length();
		return length <= 0.0001f ? 1.0f : length;
	}

	// TransformのworldMatrixからワールドスケールを取り出す
	Engine::Vector3 ExtractWorldScale(const Engine::TransformComponent& transform) {

		return Engine::Vector3(
			ExtractScale(transform.worldMatrix, Engine::Vector3(1.0f, 0.0f, 0.0f)),
			ExtractScale(transform.worldMatrix, Engine::Vector3(0.0f, 1.0f, 0.0f)),
			ExtractScale(transform.worldMatrix, Engine::Vector3(0.0f, 0.0f, 1.0f)));
	}

	// 形状のワールド中心を作成する
	Engine::Vector3 MakeWorldCenter(const Engine::CollisionShape& shape, const Engine::TransformComponent& transform) {

		return transform.worldMatrix.GetTranslationValue() +
			Engine::Vector3::TransferNormal(shape.offset, transform.worldMatrix);
	}

	// 形状に適用する回転行列を作成する
	Engine::Matrix4x4 MakeShapeRotationMatrix(const Engine::CollisionShape& shape,
		const Engine::TransformComponent& transform) {

		Engine::Quaternion rotation = Engine::Quaternion::FromEulerDegrees(shape.rotationDegrees);
		if (shape.useTransformRotation) {
			rotation = rotation * transform.localRotation;
		}
		return Engine::Quaternion::MakeRotateMatrix(rotation);
	}

	// 相手側Entityへ渡すためのContactを作成する
	Engine::CollisionContact MakeSwappedContact(const Engine::CollisionContact& contact) {

		Engine::CollisionContact swapped = contact;
		std::swap(swapped.self, swapped.other);
		std::swap(swapped.selfShapeIndex, swapped.otherShapeIndex);
		swapped.normal = -contact.normal;
		return swapped;
	}

	// Entityを移動し、Transform更新対象にする
	void MoveEntity(Engine::ECSWorld& world, const Engine::Entity& entity, const Engine::Vector3& delta) {

		if (!world.IsAlive(entity) || !world.HasComponent<Engine::TransformComponent>(entity)) {
			return;
		}
		auto& transform = world.GetComponent<Engine::TransformComponent>(entity);
		transform.localPos += delta;
		Engine::MarkTransformSubtreeDirty(world, entity);

		// 押し戻し直後にworldMatrixも更新する
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

	// 慣性半径の近似、回転の効きを決める
	constexpr float kInertiaRadius = 0.5f;
	// 数値暴走を防ぐ角速度の上限 rad/s
	constexpr float kMaxAngularSpeed = 30.0f;
	// 接地中のころがり抵抗の強さ、frictionに掛けて1フレームあたりの減衰率にする
	constexpr float kRollingResist = 0.2f;
	// この角速度以下のとき面の整列を効かせる rad/s
	constexpr float kSettleAngularMax = 2.5f;
	// 整列で起きる速さ、残差角に掛ける
	constexpr float kSettleRate = 5.0f;
	// 整列の角速度の上限 rad/s
	constexpr float kSettleMaxRate = 2.0f;
	// これ以下のsinはほぼ平らとみなす
	constexpr float kFlatEpsilon = 0.02f;

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

	// 3DのBox形状を持つか、整列は箱だけに効かせ球には効かせない
	bool HasBoxShape3D(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (!world.HasComponent<Engine::CollisionComponent>(entity)) {
			return false;
		}
		for (const auto& shape : world.GetComponent<Engine::CollisionComponent>(entity).shapes) {
			if (shape.enabled &&
				(shape.type == Engine::ColliderShapeType::AABB3D || shape.type == Engine::ColliderShapeType::OBB3D)) {
				return true;
			}
		}
		return false;
	}

	// 箱がほぼ静止しているのに面が傾いているとき、最寄りの面を接地面へ向ける角速度を与えて起こす
	// 単一接触点だと角で立ったまま倒れないので、整列で平らな姿勢へ収束させる
	void SettleBoxOrientation(Engine::RigidbodyComponent& body, const Engine::Matrix4x4& worldMatrix,
		const Engine::Vector3& normal) {

		if (body.angularVelocity.Length() > kSettleAngularMax) {
			return;
		}

		// 箱の世界軸を取り出す
		const Engine::Vector3 axes[3] = {
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(Engine::Vector3(1.0f, 0.0f, 0.0f), worldMatrix), Engine::Vector3(1.0f, 0.0f, 0.0f)),
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(Engine::Vector3(0.0f, 1.0f, 0.0f), worldMatrix), Engine::Vector3(0.0f, 1.0f, 0.0f)),
			Engine::Vector3::NormalizeOr(Engine::Vector3::TransferNormal(Engine::Vector3(0.0f, 0.0f, 1.0f), worldMatrix), Engine::Vector3(0.0f, 0.0f, 1.0f)),
		};

		// 接触法線に最も近い面の軸を選ぶ
		uint32_t best = 0;
		float bestDot = std::fabs(Engine::Vector3::Dot(axes[0], normal));
		for (uint32_t i = 1; i < 3; ++i) {

			const float current = std::fabs(Engine::Vector3::Dot(axes[i], normal));
			if (current > bestDot) {
				bestDot = current;
				best = i;
			}
		}

		// その軸を法線へ揃える最短回転、外積の大きさがsin
		const Engine::Vector3 nearestAxis = axes[best];
		const Engine::Vector3 target = Engine::Vector3::Dot(nearestAxis, normal) >= 0.0f ? normal : -normal;
		const Engine::Vector3 rotAxis = Engine::Vector3::Cross(nearestAxis, target);
		const float sinAngle = rotAxis.Length();
		if (sinAngle < kFlatEpsilon) {
			return;
		}

		// 残差角に比例した速度で起こす、SETで上書きしころがり抵抗に負けないようにする
		const float angle = std::asin(std::clamp(sinAngle, 0.0f, 1.0f));
		const float speed = (std::min)(angle * kSettleRate, kSettleMaxRate);
		body.angularVelocity = (rotAxis * (1.0f / sinAngle)) * speed;
	}

	// 接触面で速度を反発と摩擦で更新する、allowToppleがONなら接触点まわりの回転も解く
	void ResolveContactVelocity(Engine::ECSWorld& world, const Engine::Entity& entity,
		const Engine::Vector3& pushOutDir, const Engine::Vector3& contactPoint) {

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
			if (body.allowTopple) {

				ResolveContact3D(body, pushOutDir, lever);
				// 角で止まらないよう、ほぼ静止した箱は最寄りの面へ起こす
				if (HasBoxShape3D(world, entity)) {
					SettleBoxOrientation(body, transform.worldMatrix, pushOutDir);
				}
			} else {
				ResolveLinearOnly(body.linearVelocity, pushOutDir, body.restitution, body.friction);
			}
		}
		if (world.HasComponent<Engine::Rigidbody2DComponent>(entity)) {

			auto& body = world.GetComponent<Engine::Rigidbody2DComponent>(entity);
			if (body.bodyType != Engine::RigidbodyType::Dynamic) {
				return;
			}
			const Engine::Vector2 normal2D = Engine::Vector2(pushOutDir.x, pushOutDir.y);
			if (body.allowTopple && !body.freezeRotation) {
				ResolveContact2D(body, normal2D, Engine::Vector2(lever.x, lever.y));
			} else {
				ResolveLinearOnly(body.linearVelocity, normal2D, body.restitution, body.friction);
			}
		}
	}
}

void Engine::CollisionSystem::OnWorldExit([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	previousContacts_.clear();
}

void Engine::CollisionSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// 形状描画の衝突中フラグを毎フレーム初期化する、衝突したものだけ後で立てる
	world.ForEach<CollisionComponent>([](Entity, CollisionComponent& collision) {
		collision.runtimeColliding = false;
		});

	// 判定自体はEdit中も走らせて衝突表示を赤くする、押し戻しとコールバックはPlay中だけにする
	const bool isPlaying = (context.mode == WorldMode::Play);

	CollisionSettings& settings = CollisionSettings::GetInstance();
	if (context.activeSceneHeader) {
		settings.SetActiveSettingsAsset(context.activeSceneHeader->collisionSettings, context.assetDatabase);
	}
	settings.EnsureLoaded();

	std::vector<CollisionRuntimeEntity> entities{};
	world.ForEach<CollisionComponent, TransformComponent>([&](
		Entity entity, CollisionComponent& collision, TransformComponent& transform) {

			if (!collision.enabled || !IsEntityActiveInHierarchy(world, entity)) {
				return;
			}

			// Entityに含まれる有効形状を判定用形状へ変換する
			CollisionRuntimeEntity runtime{};
			runtime.entity = entity;
			runtime.collision = &collision;
			runtime.transform = &transform;
			for (uint32_t i = 0; i < static_cast<uint32_t>(collision.shapes.size()); ++i) {

				const CollisionShape& shape = collision.shapes[i];
				if (!shape.enabled) {
					continue;
				}
				runtime.shapes.emplace_back(BuildShapeInstance(entity, shape, i, transform));
			}
			if (!runtime.shapes.empty()) {
				entities.emplace_back(std::move(runtime));
			}
		});

	std::unordered_map<CollisionPairKey, CollisionContact, CollisionPairKeyHash> currentContacts{};
	for (uint32_t aIndex = 0; aIndex < static_cast<uint32_t>(entities.size()); ++aIndex) {
		for (uint32_t bIndex = aIndex + 1; bIndex < static_cast<uint32_t>(entities.size()); ++bIndex) {

			CollisionRuntimeEntity& a = entities[aIndex];
			CollisionRuntimeEntity& b = entities[bIndex];
			if (!settings.CanCollide(a.collision->typeMask, b.collision->typeMask)) {
				continue;
			}

			// 複数形状のうち、最も深く接触した結果をEntity間のContactとして扱う
			CollisionContact bestContact{};
			bool hasContact = false;
			for (const auto& shapeA : a.shapes) {
				for (const auto& shapeB : b.shapes) {

					CollisionContact contact{};
					if (!TestCollision(shapeA, shapeB, contact)) {
						continue;
					}
					if (!hasContact || bestContact.penetration < contact.penetration) {
						bestContact = contact;
						hasContact = true;
					}
				}
			}
			if (!hasContact) {
				continue;
			}

			const CollisionPairKey key = CollisionPairKey::Make(a.entity, b.entity);
			currentContacts[key] = bestContact;

			// 衝突中フラグを立てて形状描画を赤くする、トリガーの重なりも衝突として扱う
			if (a.collision) {
				a.collision->runtimeColliding = true;
			}
			if (b.collision) {
				b.collision->runtimeColliding = true;
			}

			// 押し戻しとEnter / Stayの分配はPlay中のみ行う
			if (isPlaying) {
				ApplyPushback(world, a, b, bestContact);
				if (previousContacts_.contains(key)) {
					DispatchCollisionStay(world, context, bestContact);
				} else {
					DispatchCollisionEnter(world, context, bestContact);
				}
			}
		}
	}

	// Edit中はコールバックも履歴も持たず、表示用フラグだけ更新して終える
	if (!isPlaying) {
		previousContacts_.clear();
		return;
	}

	// 前フレームにだけ存在した接触はExitとして扱う
	for (const auto& [key, contact] : previousContacts_) {
		if (!currentContacts.contains(key)) {
			DispatchCollisionExit(world, context, contact);
		}
	}
	previousContacts_ = std::move(currentContacts);
}

Engine::CollisionShapeInstance Engine::CollisionSystem::BuildShapeInstance(const Entity& entity,
	const CollisionShape& shape, uint32_t shapeIndex, const TransformComponent& transform) const {

	CollisionShapeInstance instance{};
	instance.entity = entity;
	instance.shapeIndex = shapeIndex;
	instance.type = shape.type;
	instance.trigger = shape.isTrigger;
	instance.center = MakeWorldCenter(shape, transform);

	// Transformのスケールを衝突サイズに反映する
	const Vector3 scale = AbsVector(ExtractWorldScale(transform));
	instance.radius = shape.radius * (std::max)({ scale.x, scale.y, scale.z });
	instance.halfExtents = Vector3(
		shape.halfExtents3D.x * scale.x,
		shape.halfExtents3D.y * scale.y,
		shape.halfExtents3D.z * scale.z);

	if (shape.type == ColliderShapeType::Circle2D) {
		instance.radius = shape.radius * (std::max)(scale.x, scale.y);
	}
	if (shape.type == ColliderShapeType::Quad2D) {
		instance.halfExtents = Vector3(shape.halfSize2D.x * scale.x, shape.halfSize2D.y * scale.y, 0.0f);
	}

	// 回転を使わない形状はワールド軸に固定する
	const bool rotate2D = shape.type == ColliderShapeType::Quad2D && shape.rotatedQuad;
	const bool rotate3D = shape.type == ColliderShapeType::OBB3D;
	if (rotate2D || rotate3D) {

		const Matrix4x4 rotation = MakeShapeRotationMatrix(shape, transform);
		instance.axes[0] = Engine::Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotation), Vector3(1.0f, 0.0f, 0.0f));
		instance.axes[1] = Engine::Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotation), Vector3(0.0f, 1.0f, 0.0f));
		instance.axes[2] = Engine::Vector3::NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotation), Vector3(0.0f, 0.0f, 1.0f));
	} else {

		instance.axes[0] = Vector3(1.0f, 0.0f, 0.0f);
		instance.axes[1] = Vector3(0.0f, 1.0f, 0.0f);
		instance.axes[2] = Vector3(0.0f, 0.0f, 1.0f);
	}
	return instance;
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

	if (movableA && movableB) {
		// 双方が動ける場合はめり込み量を半分ずつ分ける
		MoveEntity(world, a.entity, -contact.normal * (contact.penetration * 0.5f));
		MoveEntity(world, b.entity, contact.normal * (contact.penetration * 0.5f));
		ResolveContactVelocity(world, a.entity, -contact.normal, contact.point);
		ResolveContactVelocity(world, b.entity, contact.normal, contact.point);
		return;
	}
	if (movableA) {
		MoveEntity(world, a.entity, -contact.normal * contact.penetration);
		ResolveContactVelocity(world, a.entity, -contact.normal, contact.point);
		return;
	}
	if (movableB) {
		MoveEntity(world, b.entity, contact.normal * contact.penetration);
		ResolveContactVelocity(world, b.entity, contact.normal, contact.point);
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
