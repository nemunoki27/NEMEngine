#pragma once

//============================================================================
//	include
//============================================================================
#include <Engine/Core/Physics/Collision/CollisionDetection.h>
#include <Engine/Core/World/ECS/Systems/Core/ISystem.h>

namespace Engine {

	struct CollisionComponent;
	struct CollisionRuntimeStateComponent;
	struct TransformComponent;

	struct CollisionRuntimeEntity {

		Entity entity = Entity::Null();
		CollisionComponent* collision = nullptr;
		CollisionRuntimeStateComponent* state = nullptr;
		TransformComponent* transform = nullptr;
		CollisionShapeInstance shape{};
		bool hasShape = false;
		bool dynamicBody = false;
		bool surfaceBox = false;
		uint8_t internalFaces = 0;
	};
}

namespace Engine::CollisionFrameBuilder {

	// 有効な単一形状を走査順で収集
	void Collect(ECSWorld& world, std::vector<CollisionRuntimeEntity>& entities);
	// 現在のTransformから形状を再構築
	void RebuildRuntimeShape(ECSWorld& world, CollisionRuntimeEntity& runtime);
}
