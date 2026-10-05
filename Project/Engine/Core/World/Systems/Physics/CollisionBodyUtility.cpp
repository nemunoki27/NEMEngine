#include "CollisionBodyUtility.h"

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
#include <Engine/Core/World/Systems/Transform/TransformWorldUtility.h>
#include <Engine/Core/World/Scene/Serialization/SceneHeader.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <algorithm>
#include <cmath>

namespace Engine::CollisionBodyUtility {

	bool IsDynamicRigidbody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (world.HasComponent<Engine::RigidbodyComponent>(entity)) {
			return world.GetComponent<Engine::RigidbodyComponent>(entity).bodyType == Engine::RigidbodyType::Dynamic;
		}
		if (world.HasComponent<Engine::Rigidbody2DComponent>(entity)) {
			return world.GetComponent<Engine::Rigidbody2DComponent>(entity).bodyType == Engine::RigidbodyType::Dynamic;
		}
		return false;
	}

	bool HasRigidbody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		return world.HasComponent<Engine::RigidbodyComponent>(entity) ||
			   world.HasComponent<Engine::Rigidbody2DComponent>(entity);
	}

	bool IsBoxSurfaceBody(Engine::ECSWorld& world, const Engine::Entity& entity) {

		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(entity)) {
			return body->bodyType == Engine::RigidbodyType::Static;
		}
		return !world.HasComponent<Engine::Rigidbody2DComponent>(entity);
	}

	// Rigidbodyの固定軸を移動量へ反映する
	Engine::Vector3 ApplyTranslationConstraints(Engine::ECSWorld& world, const Engine::Entity& entity, Engine::Vector3 value) {

		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(entity)) {

			if (body->freezePositionX) {
				value.x = 0.0f;
			}
			if (body->freezePositionY) {
				value.y = 0.0f;
			}
			if (body->freezePositionZ) {
				value.z = 0.0f;
			}
		}
		if (const auto* body = world.TryGetComponent<Engine::Rigidbody2DComponent>(entity)) {

			if (body->freezePositionX) {
				value.x = 0.0f;
			}
			if (body->freezePositionY) {
				value.y = 0.0f;
			}
		}
		return value;
	}

	// Transform変更をworldMatrixへ即時反映する
	void UpdateTransformWorldMatrix(
		Engine::ECSWorld& world, const Engine::Entity& entity, Engine::TransformComponent& transform) {

		Engine::MarkTransformSubtreeDirty(world, entity);

		// 親の継承設定とJointを反映する
		Engine::ResolvedWorldTransform parentFollow{};
		if (!Engine::TransformWorldUtility::ResolveParentFollowTransform(world, entity, parentFollow)) {
			return;
		}
		const Engine::Matrix4x4 localMatrix =
			Engine::Matrix4x4::MakeAffineMatrix(transform.localScale, transform.localRotation, transform.localPos);
		transform.worldMatrix = localMatrix * parentFollow.matrix;
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
		// Worldの補正量を親追従座標へ変換する
		Engine::ResolvedWorldTransform parentFollow{};
		Engine::Matrix4x4 inverseParent{};
		if (!Engine::TransformWorldUtility::ResolveParentFollowTransform(world, entity, parentFollow) ||
			!Engine::Matrix4x4::TryInverse(parentFollow.matrix, inverseParent)) {
			return;
		}
		const Engine::Vector3 localDelta = Engine::Vector3::TransferNormal(constrainedDelta, inverseParent);
		transform.localPos += localDelta;
		UpdateTransformWorldMatrix(world, entity, transform);
	}

	float ResolveInverseMass(Engine::ECSWorld& world, Engine::Entity entity) {

		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(entity)) {
			return body->bodyType == Engine::RigidbodyType::Dynamic ? 1.0f / (std::max)(body->mass, 0.0001f) : 0.0f;
		}
		if (const auto* body = world.TryGetComponent<Engine::Rigidbody2DComponent>(entity)) {
			return body->bodyType == Engine::RigidbodyType::Dynamic ? 1.0f / (std::max)(body->mass, 0.0001f) : 0.0f;
		}
		return 1.0f;
	}
}
