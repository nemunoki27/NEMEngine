#include "JointConstraintSystem.h"

//============================================================================
//	include
//============================================================================
#include <Engine/Core/World/Components/Physics/PhysicsJointComponent.h>
#include <Engine/Core/World/Components/Physics/RigidbodyComponent.h>
#include <Engine/Core/World/Components/Physics/Rigidbody2DComponent.h>
#include <Engine/Core/World/Components/Transform/TransformComponent.h>
#include <Engine/Core/World/Components/Transform/HierarchyComponent.h>
#include <Engine/Core/World/Components/Scene/SceneObjectComponent.h>
#include <Engine/Core/World/Scene/Utility/SceneObjectUtility.h>
#include <Engine/Core/World/Systems/Hierarchy/HierarchySystem.h>
#include <Engine/Core/Foundation/Math/Matrix4x4.h>
#include <Engine/Core/Foundation/Math/Quaternion.h>

// c++
#include <algorithm>
#include <cmath>

namespace {

	Engine::Entity ResolveConnectedBody(Engine::ECSWorld& world,
		Engine::Entity owner, Engine::UUID localFileID) {

		if (!localFileID) {
			return Engine::Entity::Null();
		}
		const auto* sceneObject = world.TryGetComponent<Engine::SceneObjectComponent>(owner);
		return sceneObject ? Engine::SceneObjectUtility::FindByLocalFileID(
			world, sceneObject->sceneInstanceID, localFileID) : Engine::Entity::Null();
	}

	float InverseMass(Engine::ECSWorld& world, Engine::Entity entity) {

		if (const auto* body = world.TryGetComponent<Engine::RigidbodyComponent>(entity)) {
			return body->bodyType == Engine::RigidbodyType::Dynamic ?
				1.0f / (std::max)(body->mass, 0.0001f) : 0.0f;
		}
		if (const auto* body = world.TryGetComponent<Engine::Rigidbody2DComponent>(entity)) {
			return body->bodyType == Engine::RigidbodyType::Dynamic ?
				1.0f / (std::max)(body->mass, 0.0001f) : 0.0f;
		}
		return 0.0f;
	}

	Engine::Vector3 ToWorldPoint(const Engine::TransformComponent& transform,
		const Engine::Vector3& localPoint) {

		return Engine::Vector3::Transform(localPoint, transform.worldMatrix);
	}

	void UpdateWorldMatrix(Engine::ECSWorld& world, Engine::Entity entity,
		Engine::TransformComponent& transform) {

		Engine::Matrix4x4 parentWorld = Engine::Matrix4x4::Identity();
		if (const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(entity);
			hierarchy && world.IsAlive(hierarchy->parent)) {

			if (const auto* parent = world.TryGetComponent<Engine::TransformComponent>(
				hierarchy->parent)) {
				parentWorld = parent->worldMatrix;
			}
		}
		transform.worldMatrix = Engine::Matrix4x4::MakeAffineMatrix(
			transform.localScale, transform.localRotation,
			transform.localPos) * parentWorld;
		Engine::MarkTransformSubtreeDirty(world, entity);
	}

	void MoveWorld(Engine::ECSWorld& world, Engine::Entity entity, const Engine::Vector3& worldDelta) {

		auto* transform = world.TryGetComponent<Engine::TransformComponent>(entity);
		if (!transform) {
			return;
		}
		Engine::Vector3 localDelta = worldDelta;
		if (const auto* hierarchy = world.TryGetComponent<Engine::HierarchyComponent>(entity);
			hierarchy && world.IsAlive(hierarchy->parent)) {
			if (const auto* parent = world.TryGetComponent<Engine::TransformComponent>(hierarchy->parent)) {
				localDelta = Engine::Vector3::TransferNormal(
					worldDelta, Engine::Matrix4x4::Inverse(parent->worldMatrix));
			}
		}
		transform->localPos += localDelta;
		UpdateWorldMatrix(world, entity, *transform);
	}

	template <typename Joint>
	Engine::Entity SolveAnchor(Engine::ECSWorld& world, Engine::Entity owner, Joint& joint) {

		if (!joint.enabled) {
			return Engine::Entity::Null();
		}
		const Engine::Entity connected = ResolveConnectedBody(
			world, owner, joint.connectedBodyLocalFileID);
		auto* ownerTransform = world.TryGetComponent<Engine::TransformComponent>(owner);
		auto* connectedTransform = world.TryGetComponent<Engine::TransformComponent>(connected);
		if (!ownerTransform || !connectedTransform) {
			return Engine::Entity::Null();
		}

		if (joint.autoConfigureConnectedAnchor && !joint.runtimeInitialized) {
			const Engine::Vector3 worldAnchor = ToWorldPoint(*ownerTransform, joint.anchor);
			joint.connectedAnchor = Engine::Vector3::Transform(
				worldAnchor, Engine::Matrix4x4::Inverse(connectedTransform->worldMatrix));
			joint.runtimeRelativeRotation = Engine::Quaternion::Inverse(
				connectedTransform->localRotation) * ownerTransform->localRotation;
			joint.runtimeInitialized = true;
		}

		const Engine::Vector3 ownerAnchor = ToWorldPoint(*ownerTransform, joint.anchor);
		const Engine::Vector3 connectedAnchor = ToWorldPoint(*connectedTransform, joint.connectedAnchor);
		const Engine::Vector3 error = connectedAnchor - ownerAnchor;
		const float ownerInverseMass = InverseMass(world, owner);
		const float connectedInverseMass = InverseMass(world, connected);
		const float inverseMassSum = ownerInverseMass + connectedInverseMass;
		if (inverseMassSum <= 0.0f) {
			return Engine::Entity::Null();
		}

		MoveWorld(world, owner, error * (ownerInverseMass / inverseMassSum));
		MoveWorld(world, connected, -error * (connectedInverseMass / inverseMassSum));
		return connected;
	}

	void SolveFixedRotation(Engine::ECSWorld& world, Engine::Entity owner,
		Engine::Entity connected, const Engine::FixedJointComponent& joint) {

		auto* ownerTransform = world.TryGetComponent<Engine::TransformComponent>(owner);
		const auto* connectedTransform = world.TryGetComponent<Engine::TransformComponent>(connected);
		if (!ownerTransform || !connectedTransform || InverseMass(world, owner) <= 0.0f) {
			return;
		}
		ownerTransform->localRotation = Engine::Quaternion::Normalize(
			connectedTransform->localRotation * joint.runtimeRelativeRotation);
		UpdateWorldMatrix(world, owner, *ownerTransform);
	}

	void SolveHingeRotation(Engine::ECSWorld& world, Engine::Entity owner,
		const Engine::HingeJointComponent& joint) {

		auto* transform = world.TryGetComponent<Engine::TransformComponent>(owner);
		if (!transform || !joint.useLimits || InverseMass(world, owner) <= 0.0f) {
			return;
		}
		Engine::Vector3 euler = Engine::Quaternion::ToEulerDegrees(transform->localRotation);
		const Engine::Vector3 axis(
			std::fabs(joint.axis.x), std::fabs(joint.axis.y), std::fabs(joint.axis.z));
		float* angle = axis.y < axis.x && axis.z < axis.x ? &euler.x :
			(axis.z < axis.y ? &euler.y : &euler.z);
		*angle = std::clamp(*angle, joint.minAngle, joint.maxAngle);
		transform->localRotation = Engine::Quaternion::FromEulerDegrees(euler);
		UpdateWorldMatrix(world, owner, *transform);
	}
}

void Engine::JointConstraintSystem::FixedUpdate(ECSWorld& world, SystemContext& context) {

	if (context.mode != WorldMode::Play) {
		return;
	}
	constexpr uint32_t kJointIterations = 6;
	for (uint32_t iteration = 0; iteration < kJointIterations; ++iteration) {
		world.ForEach<FixedJointComponent>([&](Entity entity, FixedJointComponent& joint) {
			const Entity connected = SolveAnchor(world, entity, joint);
			SolveFixedRotation(world, entity, connected, joint);
			});
		world.ForEach<HingeJointComponent>([&](Entity entity, HingeJointComponent& joint) {
			SolveAnchor(world, entity, joint);
			SolveHingeRotation(world, entity, joint);
			});
	}
}
