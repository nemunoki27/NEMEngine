#include "TestContracts.h"
#include "TestFixtures.h"

#include "ApplicationPlatformTests.h"

//============================================================================
//	include
//============================================================================
#include "FoundationTests.h"
#include "EditorRefactoringTests.h"
#include "GameplayRefactoringTests.h"
#include "SceneStorageTests.h"
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Scene/Authoring/SceneAuthoring.h>
#include <Engine/Core/World/ECS/Storage/ECSStorage.h>
#include <Engine/Core/World/ECS/Systems/Scheduler/SystemScheduler.h>
#include <Engine/Core/World/Systems/Physics/CollisionSystem.h>
#include <Engine/Core/World/Systems/Physics/PhysicsSystem.h>
#include <Engine/Core/World/Systems/Transform/TransformSystem.h>

// c++
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <utility>

namespace NEMTests {

	bool TestBoxInternalFaces() {

		using namespace Engine;
		CollisionShapeInstance lower{}, upper{}, player{};
		lower.type = upper.type = ColliderShapeType::OBB3D;
		lower.halfExtents = upper.halfExtents = Vector3(0.5f, 0.5f, 4.0f);
		upper.center.y = 1.0f;
		player.type = ColliderShapeType::AABB3D;
		player.halfExtents = Vector3::AnyInit(0.225f);
		player.center = Vector3(-0.70f, 0.28f, 0.0f);
		std::array<CollisionBoxSurface, 2> surfaces{ CollisionBoxSurface{ &lower, 1 }, { &upper, 1 } };
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces != 8 || surfaces[1].internalFaces != 4) {
			return false;
		}
		CollisionContact raw{}, corrected{}, swapped{};
		if (!TestCollision(player, upper, raw) || std::abs(raw.normal.y) < 0.99f ||
			!TestCollisionWithBoxInternalFaces(player, upper, 0, surfaces[1].internalFaces, corrected) ||
			!TestCollisionWithBoxInternalFaces(upper, player, surfaces[1].internalFaces, 0, swapped) ||
			corrected.normal.x < 0.99f || std::abs(corrected.normal.y) > 0.0001f ||
			(corrected.normal + swapped.normal).Length() > 0.0001f ||
			std::abs(corrected.penetration - 0.025f) > 0.0001f ||
			std::abs(corrected.point.x + 0.5f) > 0.0001f) {
			return false;
		}
		// 内部面方向に離れている場合は応答候補の有無に関係なく非接触にする
		player.center.y = 0.0f;
		if (TestCollisionWithBoxInternalFaces(player, upper, 0, surfaces[1].internalFaces, corrected)) {
			return false;
		}
		// 列の本当の上端と下端は床と天井として残す
		player.center = Vector3(0.0f, 1.70f, 0.0f);
		if (!TestCollisionWithBoxInternalFaces(player, upper, 0, surfaces[1].internalFaces, corrected) ||
			corrected.normal.y > -0.99f) {
			return false;
		}
		player.center.y = -0.70f;
		if (!TestCollisionWithBoxInternalFaces(player, lower, 0, surfaces[0].internalFaces, corrected) ||
			corrected.normal.y < 0.99f) {
			return false;
		}
		// 部分的な段差は露出する面を残す
		upper.halfExtents.x = 0.25f;
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces != 0 || surfaces[1].internalFaces != 4) {
			return false;
		}
		upper.halfExtents.x = 0.5f;
		upper.center.y = 1.001f;
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces || surfaces[1].internalFaces) {
			return false;
		}
		// 異なるタイプとTriggerは隣の面を覆わない
		upper.center.y = 1.0f;
		surfaces[1].typeMask = 2;
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces || surfaces[1].internalFaces) {
			return false;
		}
		surfaces[1].typeMask = 1;
		upper.trigger = true;
		BuildBoxInternalFaces(surfaces);
		player.center = Vector3(-0.70f, 0.28f, 0.0f);
		if (surfaces[0].internalFaces || surfaces[1].internalFaces ||
			!TestCollisionWithBoxInternalFaces(player, upper, 0, 4, corrected) ||
			!corrected.trigger || corrected.normal.y < 0.99f) {
			return false;
		}
		upper.trigger = false;
		// フィルム移動後も前の面情報を残さず、復帰時に再構築する
		upper.center.z = 10.0f;
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces || surfaces[1].internalFaces) {
			return false;
		}
		upper.center.z = 0.0f;
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces != 8 || surfaces[1].internalFaces != 4) {
			return false;
		}
		// 軸が揃った回転箱も同じ内部面を持つ
		const float c = std::sqrt(0.5f);
		lower.axes[0] = upper.axes[0] = Vector3(c, c, 0.0f);
		lower.axes[1] = upper.axes[1] = Vector3(-c, c, 0.0f);
		upper.center = upper.axes[1];
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces != 8 || surfaces[1].internalFaces != 4) {
			return false;
		}
		// 向きの異なるPlayerは通常判定を維持する
		player.center = upper.center + Vector3(-0.5f, 0.0f, 0.0f);
		if (!TestCollision(player, upper, raw) ||
			!TestCollisionWithBoxInternalFaces(player, upper, 0, 4, corrected) ||
			(raw.normal - corrected.normal).Length() > 0.0001f) {
			return false;
		}
		upper.axes[0] = Vector3(1.0f, 0.0f, 0.0f);
		upper.axes[1] = Vector3(0.0f, 1.0f, 0.0f);
		BuildBoxInternalFaces(surfaces);
		if (surfaces[0].internalFaces || surfaces[1].internalFaces) {
			return false;
		}
		// 安全な代替面がない場合も、通常の接触を消さない
		player.center = upper.center;
		if (!TestCollision(player, upper, raw) ||
			!TestCollisionWithBoxInternalFaces(player, upper, 0, 63, corrected) ||
			(raw.normal - corrected.normal).Length() > 0.0001f ||
			std::abs(raw.penetration - corrected.penetration) > 0.0001f) {
			return false;
		}
		// 格子の角でX/Yが内部面でも、遠いZ面まで押し出さない
		upper.center = Vector3(1.0f, 1.0f, 0.0f);
		upper.halfExtents = Vector3(0.5f, 0.5f, 2.0f);
		player.center = Vector3(0.28f, 0.28f, 0.0f);
		if (!TestCollisionWithBoxInternalFaces(player, upper, 0, 5, corrected) ||
			!TestCollisionWithBoxInternalFaces(upper, player, 5, 0, swapped) ||
			std::abs(corrected.normal.z) > 0.0001f || corrected.penetration > 0.01f ||
			(corrected.normal + swapped.normal).Length() > 0.0001f) {
			return false;
		}
		// 内部面を持つ床でも、上面への通常接触は点も含めて維持する
		player.center = Vector3(1.0f, 1.70f, 0.0f);
		if (!TestCollision(player, upper, raw) ||
			!TestCollisionWithBoxInternalFaces(player, upper, 0, 5, corrected) ||
			(raw.normal - corrected.normal).Length() > 0.0001f ||
			(raw.point - corrected.point).Length() > 0.0001f ||
			std::abs(raw.penetration - corrected.penetration) > 0.0001f) {
			return false;
		}
		// 分散配置で全組み合わせを追加走査しない
		std::vector<CollisionShapeInstance> boxes(256);
		std::vector<CollisionBoxSurface> sparse;
		for (size_t i = 0; i < boxes.size(); ++i) {
			boxes[i].type = ColliderShapeType::AABB3D;
			boxes[i].center.y = static_cast<float>(i) * 3.0f;
			sparse.push_back({ &boxes[i], 1 });
		}
		return BuildBoxInternalFaces(sparse) == 0;
	}

	bool TestRigidbodyBoxSeams() {

		using namespace Engine;
		for (bool reverse : { false, true }) {
			for (bool horizontal : { false, true }) {
				ECSWorld world(ECSWorldKind::Runtime);
				auto createBox = [&](Vector3 position, Vector3 halfSize) {
					const Entity entity = SceneAuthoring::CreateGameObject(world, "BoxSeam");
					auto& transform = world.GetComponent<TransformComponent>(entity);
					transform.localPos = position;
					MarkTransformSubtreeDirty(world, entity);
					auto& collision = world.AddComponent<CollisionComponent>(entity);
					collision.enabled = true;
					collision.enablePushback = true;
					collision.isStatic = false;
					collision.shape.type = ColliderShapeType::OBB3D;
					collision.shape.halfExtents3D = halfSize;
					return entity;
				};
				Entity player = Entity::Null();
				auto createPlayer = [&]() {
					player = createBox(Vector3::AnyInit(0.0f), Vector3::AnyInit(0.225f));
					auto& body = world.AddComponent<RigidbodyComponent>(player);
					body.bodyType = RigidbodyType::Dynamic;
					body.friction = 0.0f;
					body.restitution = 0.0f;
					body.allowTopple = false;
				};
				if (!reverse) { createPlayer(); }
				for (int i = 0; i < 4; ++i) {
					const float offset = static_cast<float>(reverse ? 3 - i : i);
					createBox(horizontal ? Vector3(offset, 0.0f, 0.0f) : Vector3(0.0f, offset, 0.0f),
						Vector3(0.5f, 0.5f, 4.0f));
				}
				if (reverse) { createPlayer(); }
				SystemContext context{};
				context.mode = WorldMode::Play;
				context.fixedDeltaTime = 1.0f / 60.0f;
				TransformSystem transforms;
				CollisionSystem collisions;
				transforms.OnWorldEnter(world, context);
				bool passed = true;
				for (float side : { -1.0f, 1.0f }) {
					for (float direction : { -1.0f, 1.0f }) {
						for (int step = 0; step < 151; ++step) {
							const float travel = direction > 0.0f ? step * 0.02f : 3.0f - step * 0.02f;
							auto& transform = world.GetComponent<TransformComponent>(player);
							transform.localPos = horizontal ? Vector3(travel, side * 0.70f, 0.0f) :
								Vector3(side * 0.70f, travel, 0.0f);
							MarkTransformSubtreeDirty(world, player);
							auto& body = world.GetComponent<RigidbodyComponent>(player);
							body.linearVelocity = horizontal ? Vector3(direction * 3.0f, -side, 0.0f) :
								Vector3(-side, direction * 3.0f, 0.0f);
							transforms.FixedUpdate(world, context);
							collisions.FixedUpdate(world, context);
							const auto position = world.GetComponent<TransformComponent>(player).localPos;
							const auto velocity = world.GetComponent<RigidbodyComponent>(player).linearVelocity;
							passed &= std::abs((horizontal ? position.x : position.y) - travel) <= 0.0001f &&
								std::abs((horizontal ? velocity.x : velocity.y) - direction * 3.0f) <= 0.0001f &&
								(horizontal ? position.y : position.x) * side > 0.70f;
						}
					}
				}
				collisions.OnWorldExit(world, context);
				transforms.OnWorldExit(world, context);
				if (!passed) {
					return false;
				}
			}
		}
		return true;
	}

	bool TestBoxSeamNeighborState() {

		using namespace Engine;
		ECSWorld world(ECSWorldKind::Runtime);
		auto createBox = [&](float y) {
			const Entity entity = SceneAuthoring::CreateGameObject(world, "SeamNeighbor");
			world.GetComponent<TransformComponent>(entity).localPos.y = y;
			MarkTransformSubtreeDirty(world, entity);
			auto& collision = world.AddComponent<CollisionComponent>(entity);
			collision.shape.type = ColliderShapeType::AABB3D;
			collision.shape.halfExtents3D = Vector3(0.5f, 0.5f, 4.0f);
			return entity;
		};
		createBox(1.0f);
		const Entity lower = createBox(0.0f);
		const Entity player = createBox(0.0f);
		world.GetComponent<CollisionComponent>(player).shape.halfExtents3D = Vector3::AnyInit(0.225f);
		auto& initialBody = world.AddComponent<RigidbodyComponent>(player);
		initialBody.friction = 0.0f;
		initialBody.restitution = 0.0f;
		initialBody.allowTopple = false;
		SystemContext context{};
		context.mode = WorldMode::Play;
		TransformSystem transforms;
		CollisionSystem collisions;
		transforms.OnWorldEnter(world, context);
		auto step = [&](bool seamExpected) {
			world.GetComponent<TransformComponent>(player).localPos = Vector3(-0.70f, 0.28f, 0.0f);
			MarkTransformSubtreeDirty(world, player);
			world.GetComponent<RigidbodyComponent>(player).linearVelocity = Vector3(1.0f, 3.0f, 0.0f);
			transforms.FixedUpdate(world, context);
			collisions.FixedUpdate(world, context);
			const float velocity = world.GetComponent<RigidbodyComponent>(player).linearVelocity.y;
			return std::abs(velocity - (seamExpected ? 3.0f : 0.0f)) <= 0.0001f;
		};
		bool passed = step(true);
		world.GetComponent<CollisionComponent>(lower).enabled = false;
		passed &= step(false);
		world.GetComponent<CollisionComponent>(lower).enabled = true;
		passed &= step(true);
		world.GetComponent<CollisionComponent>(lower).shape.isTrigger = true;
		passed &= step(false);
		world.GetComponent<CollisionComponent>(lower).shape.isTrigger = false;
		world.GetComponent<TransformComponent>(lower).localPos.z = 20.0f;
		MarkTransformSubtreeDirty(world, lower);
		passed &= step(false);
		world.GetComponent<TransformComponent>(lower).localPos.z = 0.0f;
		MarkTransformSubtreeDirty(world, lower);
		passed &= step(true);
		world.AddComponent<RigidbodyComponent>(lower).bodyType = RigidbodyType::Kinematic;
		passed &= step(false);
		world.GetComponent<RigidbodyComponent>(lower).bodyType = RigidbodyType::Static;
		passed &= step(true);
		collisions.OnWorldExit(world, context);
		transforms.OnWorldExit(world, context);
		return passed;
	}

	bool TestBoxSeamLanding() {

		using namespace Engine;
		for (bool reverse : { false, true }) {
			ECSWorld world(ECSWorldKind::Runtime);
			auto createBox = [&](Vector3 position, Vector3 halfSize) {
				const Entity entity = SceneAuthoring::CreateGameObject(world, "SeamLanding");
				world.GetComponent<TransformComponent>(entity).localPos = position;
				MarkTransformSubtreeDirty(world, entity);
				auto& collision = world.AddComponent<CollisionComponent>(entity);
				collision.shape.type = ColliderShapeType::AABB3D;
				collision.shape.halfExtents3D = halfSize;
				return entity;
			};
			Entity player = Entity::Null();
			auto createPlayer = [&]() {
				player = createBox(Vector3(0.5f, 3.0f, 0.0f), Vector3::AnyInit(0.225f));
				auto& body = world.AddComponent<RigidbodyComponent>(player);
				body.bodyType = RigidbodyType::Dynamic;
				body.friction = 0.0f;
				body.restitution = 0.0f;
				body.allowTopple = false;
				body.useGravity = true;
			};
			if (!reverse) { createPlayer(); }
			for (int i = 0; i < 4; ++i) {
				const int cell = reverse ? 3 - i : i;
				createBox(Vector3(static_cast<float>(cell % 2), static_cast<float>(cell / 2), 0.0f),
					Vector3(0.5f, 0.5f, 2.0f));
			}
			if (reverse) { createPlayer(); }
			SystemContext context{};
			context.mode = WorldMode::Play;
			context.fixedDeltaTime = 1.0f / 60.0f;
			TransformSystem transforms;
			PhysicsSystem physics;
			CollisionSystem collisions;
			transforms.OnWorldEnter(world, context);
			bool passed = true;
			for (int step = 0; step < 300; ++step) {
				physics.FixedUpdate(world, context);
				transforms.FixedUpdate(world, context);
				collisions.FixedUpdate(world, context);
				const auto position = world.GetComponent<TransformComponent>(player).localPos;
				passed &= position.y > 1.70f && std::abs(position.z) < 0.0001f;
			}
			passed &= world.GetComponent<TransformComponent>(player).localPos.y < 1.73f;
			passed &= std::abs(world.GetComponent<RigidbodyComponent>(player).linearVelocity.y) < 0.0001f;
			// 深く重なった格子内でも、内部面を除いた結果Z方向へ脱落させない
			world.GetComponent<TransformComponent>(player).localPos = Vector3(0.28f, 0.28f, 0.0f);
			MarkTransformSubtreeDirty(world, player);
			transforms.FixedUpdate(world, context);
			collisions.FixedUpdate(world, context);
			passed &= std::abs(world.GetComponent<TransformComponent>(player).localPos.z) < 0.0001f;
			collisions.OnWorldExit(world, context);
			transforms.OnWorldExit(world, context);
			if (!passed) { return false; }
		}
		return true;
	}
}
