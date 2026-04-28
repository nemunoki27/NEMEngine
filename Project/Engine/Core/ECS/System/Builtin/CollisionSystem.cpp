#include "CollisionSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Collision/CollisionSettings.h>
#include <Engine/Core/ECS/Component/Builtin/CollisionComponent.h>
#include <Engine/Core/ECS/Component/Builtin/TransformComponent.h>
#include <Engine/Core/ECS/Component/Builtin/SceneObjectComponent.h>
#include <Engine/Core/ECS/System/Builtin/BehaviorSystem.h>
#include <Engine/Core/ECS/System/Builtin/HierarchySystem.h>
#include <Engine/MathLib/Matrix4x4.h>
#include <Engine/MathLib/Quaternion.h>

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

	// 長さが0に近い場合はfallbackを返す
	Engine::Vector3 NormalizeOr(const Engine::Vector3& value, const Engine::Vector3& fallback) {

		const float length = value.Length();
		if (length <= 0.0001f) {
			return fallback;
		}
		return value / length;
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
	}
}

void Engine::CollisionSystem::OnWorldExit([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	previousContacts_.clear();
}

void Engine::CollisionSystem::LateUpdate(ECSWorld& world, SystemContext& context) {

	// Play中のみ衝突判定とコールバックを実行する
	if (context.mode != WorldMode::Play) {
		previousContacts_.clear();
		return;
	}

	CollisionSettings& settings = CollisionSettings::GetInstance();
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
			ApplyPushback(world, a, b, bestContact);

			// 前フレームの接触履歴からEnter / Stayを分ける
			if (previousContacts_.contains(key)) {
				DispatchCollisionStay(world, context, bestContact);
			} else {
				DispatchCollisionEnter(world, context, bestContact);
			}
		}
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
		instance.axes[0] = NormalizeOr(Vector3::TransferNormal(Vector3(1.0f, 0.0f, 0.0f), rotation), Vector3(1.0f, 0.0f, 0.0f));
		instance.axes[1] = NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 1.0f, 0.0f), rotation), Vector3(0.0f, 1.0f, 0.0f));
		instance.axes[2] = NormalizeOr(Vector3::TransferNormal(Vector3(0.0f, 0.0f, 1.0f), rotation), Vector3(0.0f, 0.0f, 1.0f));
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

	const bool movableA = !a.collision->isStatic && a.collision->enablePushback;
	const bool movableB = !b.collision->isStatic && b.collision->enablePushback;
	if (!movableA && !movableB) {
		return;
	}

	if (movableA && movableB) {
		// 双方が動ける場合はめり込み量を半分ずつ分ける
		MoveEntity(world, a.entity, -contact.normal * (contact.penetration * 0.5f));
		MoveEntity(world, b.entity, contact.normal * (contact.penetration * 0.5f));
		return;
	}
	if (movableA) {
		MoveEntity(world, a.entity, -contact.normal * contact.penetration);
		return;
	}
	if (movableB) {
		MoveEntity(world, b.entity, contact.normal * contact.penetration);
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
