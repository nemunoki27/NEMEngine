#include "CollisionSystem.h"

//============================================================================
//	include
//============================================================================
#include "CollisionResponse.h"
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

namespace {

	bool GetContinuousDelta(Engine::ECSWorld& world,
		const Engine::CollisionRuntimeEntity& runtime, Engine::Vector3& outDelta) {

		const Engine::Vector3 current = runtime.transform->worldMatrix.GetTranslationValue();
		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(runtime.entity);
			body && body->collisionDetection == Engine::CollisionDetectionMode::Continuous &&
			body->hasPreviousWorldPosition) {
			outDelta = current - body->previousWorldPosition;
			return true;
		}
		if (const auto* body = world.TryGetComponent<Engine::Rigidbody2DComponent>(runtime.entity);
			body && body->collisionDetection == Engine::CollisionDetectionMode::Continuous &&
			body->hasPreviousWorldPosition) {
			outDelta = current - body->previousWorldPosition;
			return true;
		}
		return false;
	}

	void MoveShape(Engine::CollisionShapeInstance& shape, const Engine::Vector3& delta) {

		shape.center += delta;
		shape.segmentStart += delta;
		shape.segmentEnd += delta;
	}

	bool TestContinuousCollision(Engine::ECSWorld& world,
		Engine::CollisionRuntimeEntity& a, Engine::CollisionRuntimeEntity& b,
		Engine::CollisionContact& outContact) {

		Engine::Vector3 deltaA{};
		Engine::Vector3 deltaB{};
		const bool continuousA = GetContinuousDelta(world, a, deltaA);
		const bool continuousB = GetContinuousDelta(world, b, deltaB);
		if (!continuousA && !continuousB) {
			return false;
		}

		const float relativeDistance = (deltaA - deltaB).Length();
		const float featureA = (std::max)((std::min)({
			a.shape.halfExtents.x, a.shape.halfExtents.y, a.shape.halfExtents.z, a.shape.radius }), 0.02f);
		const float featureB = (std::max)((std::min)({
			b.shape.halfExtents.x, b.shape.halfExtents.y, b.shape.halfExtents.z, b.shape.radius }), 0.02f);
		const uint32_t steps = std::clamp(static_cast<uint32_t>(
			std::ceil(relativeDistance / (std::min)(featureA, featureB))), 1u, 32u);
		for (uint32_t step = 0; step < steps; ++step) {
			const float time = static_cast<float>(step) / static_cast<float>(steps);
			Engine::CollisionShapeInstance shapeA = a.shape;
			Engine::CollisionShapeInstance shapeB = b.shape;
			MoveShape(shapeA, -deltaA * (1.0f - time));
			MoveShape(shapeB, -deltaB * (1.0f - time));
			if (!Engine::TestCollision(shapeA, shapeB, outContact)) {
				continue;
			}

			// Triggerは通過を通知し、剛体だけ衝突時刻まで戻す
			if (!outContact.trigger) {
				if (continuousA) {
					Engine::CollisionResponse::MoveByWorldDelta(world, a,
						-deltaA * (1.0f - time));
				}
				if (continuousB) {
					Engine::CollisionResponse::MoveByWorldDelta(world, b,
						-deltaB * (1.0f - time));
				}
				Engine::TestCollision(a.shape, b.shape, outContact);
			}
			return true;
		}
		return false;
	}
}

//============================================================================
//	CollisionSystem classMethods
//============================================================================

void Engine::CollisionSystem::OnWorldExit([[maybe_unused]] ECSWorld& world, [[maybe_unused]] SystemContext& context) {

	history_.Clear();
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
	CollisionFrameBuilder::Collect(world, entities);

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
	std::unordered_map<CollisionPairKey, CollisionContact, CollisionPairKeyHash> currentContacts{};
	std::vector<CollisionContact> contactNotifications{};
	constexpr uint32_t kSolverIterations = 6;
	const uint32_t iterationCount = applyResponse ? kSolverIterations : 1;
	for (uint32_t iteration = 0; iteration < iterationCount; ++iteration) {

		bool surfaceGeometryChanged = false;
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
				bool colliding = (facesA | facesB) ?
					TestCollisionWithBoxInternalFaces(a.shape, b.shape, facesA, facesB, contact) :
					TestCollision(a.shape, b.shape, contact);
				if (!colliding && iteration == 0) {
					colliding = TestContinuousCollision(world, a, b, contact);
				}
				if (!colliding) {
					continue;
				}

				const CollisionPairKey key = CollisionPairKey::Make(a.entity, b.entity);
				if (iteration == 0) {
					currentContacts[key] = contact;
					contactNotifications.emplace_back(contact);

					// 衝突中フラグを立てて形状描画を赤くする、トリガーの重なりも衝突として扱う
					if (a.state) {
						a.state->colliding = true;
					}
					if (b.state) {
						b.state->colliding = true;
					}
				}

				// 押し戻しとEnter / Stayの分配は固定ステップのみ行う
				if (applyResponse) {
					const Vector3 centerA = a.shape.center;
					const Vector3 centerB = b.shape.center;
					CollisionResponse::ApplyPushback(world, a, b, contact);
					surfaceGeometryChanged |= (a.surfaceBox && (a.shape.center - centerA).Length() > 0.0f) ||
						(b.surfaceBox && (b.shape.center - centerB).Length() > 0.0f);
				}
			}
		}
	}

	// Edit中はコールバックも履歴も持たず、表示用フラグだけ更新して終える
	if (!applyResponse) {
		history_.Clear();
		return;
	}

	// 解法中の参照を解放してから安定した接触Snapshotを通知する
	for (const CollisionContact& contact : contactNotifications) {
		if (!world.IsAlive(contact.self) || !world.IsAlive(contact.other)) {
			continue;
		}
		const CollisionPairKey key = CollisionPairKey::Make(
			contact.self, contact.other);
		history_.NotifyContact(world, context, key, contact);
	}
	std::erase_if(currentContacts, [&](const auto& entry) {
		return !world.IsAlive(entry.second.self) ||
			!world.IsAlive(entry.second.other);
		});

	history_.Commit(world, context, std::move(currentContacts));
}
