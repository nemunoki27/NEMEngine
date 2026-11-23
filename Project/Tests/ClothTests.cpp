#include "TestContracts.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/ClothComponent.h>
#include <Engine/Core/World/Components/Physics/CollisionComponent.h>
#include <Engine/Core/World/Components/Rendering/PrimitiveRendererComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Systems/Physics/ClothCollision.h>
#include <Engine/Core/World/Systems/Physics/ClothSystem.h>

// c++
#include <cmath>
#include <iostream>

namespace {

	using namespace Engine;

	bool TestContacts() {

		ECSWorld world;
		Entity collider = world.CreateEntity(), parent = world.CreateEntity();
		world.AddComponent<TransformComponent>(parent);
		world.GetComponent<TransformComponent>(parent).localPos = {2, 3, 4};
		world.AddComponent<TransformComponent>(collider);
		world.AddComponent<HierarchyComponent>(collider);
		world.GetComponent<HierarchyComponent>(collider).parent = parent;
		world.AddComponent<CollisionComponent>(collider);
		auto& collision = world.GetComponent<CollisionComponent>(collider);
		collision.typeMask = 4u;
		const auto select = [&](uint32_t mask) {
			std::vector<CollisionShapeInstance> shapes;
			ClothCollisionQuery::Select(ClothCollisionQuery::Collect(world), mask, shapes);
			return shapes;
		};
		for (auto type : {ColliderShapeType::Sphere3D, ColliderShapeType::AABB3D, ColliderShapeType::OBB3D, ColliderShapeType::Capsule3D}) {
			collision.shape.type = type;
			collision.shape.rotationDegrees = {0, 0, 37};
			collision.shape.radius = 0.5f;
			collision.shape.halfExtents3D = {0.2f, 0.45f, 0.6f};
			collision.shape.capsuleHeight = 3.0f;
			if (!select(2u).empty() || !select(0u).empty()) return false;
			const auto shapes = select(~0u);
			if (shapes.size() != 1 || (shapes.front().center - Vector3(2, 3, 4)).Length() > 0.0001f) return false;
			const auto& shape = shapes.front();
			auto point = shape.center;
			if (type == ColliderShapeType::Capsule3D) point += (shape.segmentEnd - shape.center) * 0.25f;
			ClothCollisionQuery::Project(shapes, point, 0.025f);
			CollisionShapeInstance probe{};
			probe.radius = 0.024f;
			probe.center = point;
			CollisionContact contact;
			if (TestCollision(shape, probe, contact)) return false;
			const auto outside = point + Vector3(10, 10, 10);
			point = outside;
			ClothCollisionQuery::Project(shapes, point, 0.025f);
			if (point != outside) return false;
		}
		collision.shape.isTrigger = true;
		if (!select(4u).empty()) return false;
		collision.shape.isTrigger = false;
		collision.shape.type = ColliderShapeType::Quad2D;
		if (!select(4u).empty()) return false;
		collision.shape.type = ColliderShapeType::Sphere3D;
		collision.enabled = false;
		if (!select(4u).empty()) return false;
		world.DestroyEntity(collider);
		return select(4u).empty();
	}

	bool TestClothFloor() {

		ECSWorld world;
		const Entity owner = world.CreateEntity(), floor = world.CreateEntity();
		world.AddComponent<TransformComponent>(floor);
		world.GetComponent<TransformComponent>(floor).localPos = {0, -0.32f, 0};
		world.AddComponent<CollisionComponent>(floor);
		auto& shape = world.GetComponent<CollisionComponent>(floor).shape;
		shape.type = ColliderShapeType::AABB3D;
		shape.halfExtents3D = {2, 0.2f, 2};
		world.AddComponent<TransformComponent>(owner);
		world.AddComponent<PrimitiveRendererComponent>(owner);
		world.AddComponent<ClothComponent>(owner);
		auto& plane = world.GetComponent<PrimitiveRendererComponent>(owner).plane;
		plane.axis = PrimitivePlaneAxis::XZ;
		plane.divideX = plane.divideY = 6;
		ClothSettings settings;
		settings.windStrength = settings.gustStrength = 0.0f;
		settings.thickness = 0.02f;
		settings.collisionMask = 4u;
		world.GetComponent<CollisionComponent>(floor).typeMask = 4u;
		SetClothSettings(world, owner, settings);
		ClothSystem system;
		SystemContext context;
		context.mode = WorldMode::Play;
		context.deltaTime = context.unscaledDeltaTime = 1.0f / 60.0f;
		for (int i = 0; i < 60; ++i) system.Update(world, context);
		float lowest = 0;
		for (const auto& point : TryGetCloth(world, owner)->runtime.positions) lowest = (std::min)(lowest, point.y);
		return -0.1001f <= lowest && lowest < -0.095f &&
			world.GetComponent<TransformComponent>(floor).localPos == Vector3(0, -0.32f, 0);
	}

	bool TestCurtainPreview() {

		ECSWorld world;
		const Entity parent = world.CreateEntity(), first = world.CreateEntity(), second = world.CreateEntity();
		world.AddComponent<TransformComponent>(parent);
		ClothSettings settings;
		settings.edge = ClothEdge::Top;
		settings.pinMode = ClothPinMode::Fasteners;
		settings.foldDepth = 0.05f;
		settings.windStrength = 0.4f;
		settings.gustStrength = 0.2f;
		ClothSystem system;
		SystemContext context;
		context.mode = WorldMode::Edit;
		context.unscaledDeltaTime = 1.0f / 60.0f;
		for (const Entity cloth : {first, second}) {
			world.AddComponent<TransformComponent>(cloth);
			world.AddComponent<HierarchyComponent>(cloth);
			world.GetComponent<HierarchyComponent>(cloth).parent = parent;
			world.AddComponent<PrimitiveRendererComponent>(cloth);
			auto& plane = world.GetComponent<PrimitiveRendererComponent>(cloth).plane;
			plane.divideX = 24;
			plane.divideY = 16;
			world.AddComponent<ClothComponent>(cloth);
			SetClothSettings(world, cloth, settings);
		}
		system.Update(world, context);
		const auto& restPose = TryGetCloth(world, first)->runtime;
		if (restPose.mesh.vertices.size() != 425 || restPose.time != 0.0) return false;
		settings.playInEditMode = true;
		settings.windDirection = {0, 0, 1};
		settings.windStrength = 0.0f;
		settings.gustStrength = 6.0f;
		SetClothSettings(world, first, settings);
		settings.windDirection = {0, 0, -1};
		SetClothSettings(world, second, settings);
		for (int i = 0; i < 15; ++i) system.Update(world, context);
		const auto& firstPose = TryGetCloth(world, first)->runtime;
		const auto& secondPose = TryGetCloth(world, second)->runtime;
		if (firstPose.pinned.size() != 425 || secondPose.mesh.vertices.size() != 425 ||
			(firstPose.mesh.vertices.front().position - secondPose.mesh.vertices.front().position).Length() < 0.01f) return false;
		world.GetComponent<TransformComponent>(parent).localPos = {0.4f, 0.0f, 0.0f};
		system.Update(world, context);
		int count = 0;
		float largestFold = 0.0f;
		for (size_t i = 0; i < firstPose.pinned.size(); ++i) {
			largestFold = (std::max)(largestFold, std::abs(firstPose.rest[i].z));
			if (!firstPose.pinned[i]) continue;
			++count;
			if (i / 25 != 16 || (i % 25) % 6 != 0 ||
				(firstPose.positions[i] - firstPose.rest[i] - Vector3(0.4f, 0, 0)).Length() > 0.0001f) return false;
		}
		if (count != 5 || largestFold < 0.03f) return false;
		world.RemoveComponent<ClothComponent>(first);
		system.Update(world, context);
		return !TryGetCloth(world, first) && !TryGetCloth(world, second)->runtime.mesh.vertices.empty();
	}

	bool SampleFlag(int frameRate, std::vector<Vector3>& positions) {

		ECSWorld world;
		const Entity owner = world.CreateEntity();
		world.AddComponent<TransformComponent>(owner);
		world.AddComponent<PrimitiveRendererComponent>(owner);
		world.AddComponent<ClothComponent>(owner);
		auto& plane = world.GetComponent<PrimitiveRendererComponent>(owner);
		plane.plane.divideX = 8;
		plane.plane.divideY = 6;
		ClothSettings settings;
		settings.windStrength = settings.gustStrength = 0.0f;
		SetClothSettings(world, owner, settings);
		SystemContext context;
		context.mode = WorldMode::Play;
		ClothSystem system;
		auto tick = [&](float delta) {
			context.deltaTime = context.unscaledDeltaTime = delta;
			system.Update(world, context);
		};
		tick(0);
		for (int frame = 0; frame < frameRate; ++frame) tick(1.0f / frameRate);
		auto* record = TryGetCloth(world, owner);
		if (!record || record->runtime.mesh.vertices.size() != 63) return false;
		PrimitiveMeshData rest;
		PrimitiveMeshGenerator::Generate(plane, rest);
		bool moved = false;
		for (size_t i = 0; i < rest.vertices.size(); ++i) {
			const Vector3 position = record->runtime.mesh.vertices[i].position;
			if (!std::isfinite(position.x + position.y + position.z)) return false;
			const float distance = (position - rest.vertices[i].position).Length();
			if (i % 9 == 0 && 0.0001f < distance) return false;
			moved |= 0.01f < distance;
			positions.push_back(position);
		}
		const nlohmann::json saved = record->settings;
		auto legacy = saved;
		legacy.erase("collisionMask");
		legacy["colliders"] = nlohmann::json::array();
		if (nlohmann::json(legacy.get<ClothSettings>()) != saved || saved.contains("colliders")) return false;
		auto copy = world.CloneForSerialization();
		auto* cloned = TryGetCloth(*copy, copy->FindByUUID(world.GetUUID(owner)));
		if (!cloned || nlohmann::json(cloned->settings) != saved || !cloned->runtime.mesh.vertices.empty()) return false;
		plane.renderSpace = PrimitiveRenderSpace::Screen2D;
		tick(1.0f / frameRate);
		return moved && record->runtime.mesh.vertices.empty();
	}
}

bool NEMTests::TestCloth() {

	if (!TestContacts()) {
		std::cerr << "Cloth collider projection failed\n";
		return false;
	}
	if (!TestClothFloor()) {
		std::cerr << "Cloth floor contact failed\n";
		return false;
	}
	if (!TestCurtainPreview()) {
		std::cerr << "Cloth edit preview or curtain fasteners failed\n";
		return false;
	}
	std::vector<Engine::Vector3> reference;
	for (int rate : {30, 60, 120}) {
		std::vector<Engine::Vector3> sample;
		if (!SampleFlag(rate, sample)) {
			std::cerr << "Cloth pinning or persistence failed at " << rate << " Hz\n";
			return false;
		}
		if (reference.empty()) reference = sample;
		for (size_t i = 0; i < sample.size(); ++i) {
			if (0.002f < (sample[i] - reference[i]).Length()) {
				std::cerr << "Cloth fixed step differs at " << rate << " Hz\n";
				return false;
			}
		}
	}
	return true;
}
